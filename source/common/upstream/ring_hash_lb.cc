#include "common/upstream/ring_hash_lb.h"

#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "envoy/config/cluster/v3/cluster.pb.h"

#include "common/common/assert.h"
#include "common/upstream/load_balancer_impl.h"

#include "absl/container/inlined_vector.h"
#include "absl/strings/string_view.h"

namespace Envoy {
namespace Upstream {

RingHashLoadBalancer::RingHashLoadBalancer(
    const PrioritySet& priority_set, ClusterStats& stats, Stats::Scope& scope,
    Runtime::Loader& runtime, Runtime::RandomGenerator& random,
    const absl::optional<envoy::config::cluster::v3::Cluster::RingHashLbConfig>& config,
    const envoy::config::cluster::v3::Cluster::CommonLbConfig& common_config)
    : ThreadAwareLoadBalancerBase(priority_set, stats, runtime, random, common_config),
      scope_(scope.createScope("ring_hash_lb.")), stats_(generateStats(*scope_)),
      min_ring_size_(config ? PROTOBUF_GET_WRAPPED_OR_DEFAULT(config.value(), minimum_ring_size,
                                                              DefaultMinRingSize)
                            : DefaultMinRingSize),
      max_ring_size_(config ? PROTOBUF_GET_WRAPPED_OR_DEFAULT(config.value(), maximum_ring_size,
                                                              DefaultMaxRingSize)
                            : DefaultMaxRingSize),
      hash_function_(config ? config.value().hash_function()
                            : HashFunction::Cluster_RingHashLbConfig_HashFunction_XX_HASH),
      use_hostname_for_hashing_(
          common_config.has_consistent_hashing_lb_config()
              ? common_config.consistent_hashing_lb_config().use_hostname_for_hashing()
              : false) {
  // It's important to do any config validation here, rather than deferring to Ring's ctor,
  // because any exceptions thrown here will be caught and handled properly.
  if (min_ring_size_ > max_ring_size_) {
    throw EnvoyException(fmt::format("ring hash: minimum_ring_size ({}) > maximum_ring_size ({})",
                                     min_ring_size_, max_ring_size_));
  }
}

RingHashLoadBalancerStats RingHashLoadBalancer::generateStats(Stats::Scope& scope) {
  return {ALL_RING_HASH_LOAD_BALANCER_STATS(POOL_GAUGE(scope))};
}

HostConstSharedPtr RingHashLoadBalancer::Ring::chooseHost(uint64_t h) const {
  if (ring_.empty()) {
    return nullptr;
  }

  // Ported from https://github.com/RJ/ketama/blob/master/libketama/ketama.c (ketama_get_server)
  // I've generally kept the variable names to make the code easier to compare.
  // NOTE: The algorithm depends on using signed integers for lowp, midp, and highp. Do not
  //       change them!
  int64_t lowp = 0;
  int64_t highp = ring_.size();
  while (true) {
    int64_t midp = (lowp + highp) / 2;

    if (midp == static_cast<int64_t>(ring_.size())) {
      return ring_[0].host_;
    }

    uint64_t midval = ring_[midp].hash_;
    uint64_t midval1 = midp == 0 ? 0 : ring_[midp - 1].hash_;

    if (h <= midval && h > midval1) {
      return ring_[midp].host_;
    }

    if (midval < h) {
      lowp = midp + 1;
    } else {
      highp = midp - 1;
    }

    if (lowp > highp) {
      return ring_[0].host_;
    }
  }
}

using HashFunction = envoy::config::cluster::v3::Cluster::RingHashLbConfig::HashFunction;
RingHashLoadBalancer::Ring::Ring(const NormalizedHostWeightVector& normalized_host_weights,
                                 double min_normalized_weight, uint64_t min_ring_size,
                                 uint64_t max_ring_size, HashFunction hash_function,
                                 bool use_hostname_for_hashing, RingHashLoadBalancerStats& stats)
    : stats_(stats) {
  ENVOY_LOG(trace, "ring hash: building ring");

  // We can't do anything sensible with no hosts.
  if (normalized_host_weights.empty()) {
    return;
  }

  // Scale up the number of hashes per host such that the least-weighted host gets a whole number
  // of hashes on the ring. Other hosts might not end up with whole numbers, and that's fine (the
  // ring-building algorithm below can handle this). This preserves the original implementation's
  // behavior: when weights aren't provided, all hosts should get an equal number of hashes. In
  // the case where this number exceeds the max_ring_size, it's scaled back down to fit.
  const double scale =
      std::min(std::ceil(min_normalized_weight * min_ring_size) / min_normalized_weight,
               static_cast<double>(max_ring_size));

  // Reserve memory for the entire ring up front.
  const uint64_t ring_size = std::ceil(scale);
  ring_.reserve(ring_size);

  // Populate the hash ring by walking through the (host, weight) pairs in
  // normalized_host_weights, and generating (scale * weight) hashes for each host. Since these
  // aren't necessarily whole numbers, we maintain running sums -- current_hashes and
  // target_hashes -- which allows us to populate the ring in a mostly stable way.
  //
  // For example, suppose we have 4 hosts, each with a normalized weight of 0.25, and a scale of
  // 6.0 (because the max_ring_size is 6). That means we want to generate 1.5 hashes per host.
  // We start the outer loop with current_hashes = 0 and target_hashes = 0.
  //   - For the first host, we set target_hashes = 1.5. After one run of the inner loop,
  //     current_hashes = 1. After another run, current_hashes = 2, so the inner loop ends.
  //   - For the second host, target_hashes becomes 3.0, and current_hashes is 2 from before.
  //     After only one run of the inner loop, current_hashes = 3, so the inner loop ends.
  //   - Likewise, the third host gets two hashes, and the fourth host gets one hash.
  //
  // For stats reporting, keep track of the minimum and maximum actual number of hashes per host.
  // Users should hopefully pay attention to these numbers and alert if min_hashes_per_host is too
  // low, since that implies an inaccurate request distribution.

  absl::InlinedVector<char, 196> hash_key_buffer;
  double current_hashes = 0.0;
  double target_hashes = 0.0;
  uint64_t min_hashes_per_host = ring_size;
  uint64_t max_hashes_per_host = 0;
  for (const auto& entry : normalized_host_weights) {
    const auto& host = entry.first;
    const std::string& address_string =
        use_hostname_for_hashing ? host->hostname() : host->address()->asString();
    ASSERT(!address_string.empty());

    hash_key_buffer.assign(address_string.begin(), address_string.end());
    hash_key_buffer.emplace_back('_');
    auto offset_start = hash_key_buffer.end();

    // As noted above: maintain current_hashes and target_hashes as running sums across the entire
    // host set. `i` is needed only to construct the hash key, and tally min/max hashes per host.
    target_hashes += scale * entry.second;
    uint64_t i = 0;
    while (current_hashes < target_hashes) {
      const std::string i_str = absl::StrCat("", i);
      hash_key_buffer.insert(offset_start, i_str.begin(), i_str.end());

      absl::string_view hash_key(static_cast<char*>(hash_key_buffer.data()),
                                 hash_key_buffer.size());

      const uint64_t hash =
          (hash_function == HashFunction::Cluster_RingHashLbConfig_HashFunction_MURMUR_HASH_2)
              ? MurmurHash::murmurHash2_64(hash_key, MurmurHash::STD_HASH_SEED)
              : HashUtil::xxHash64(hash_key);

      ENVOY_LOG(trace, "ring hash: hash_key={} hash={}", hash_key.data(), hash);
      ring_.push_back({hash, host});
      ++i;
      ++current_hashes;
      hash_key_buffer.erase(offset_start, hash_key_buffer.end());
    }
    min_hashes_per_host = std::min(i, min_hashes_per_host);
    max_hashes_per_host = std::max(i, max_hashes_per_host);
  }

  std::sort(ring_.begin(), ring_.end(), [](const RingEntry& lhs, const RingEntry& rhs) -> bool {
    return lhs.hash_ < rhs.hash_;
  });
  if (ENVOY_LOG_CHECK_LEVEL(trace)) {
    for (const auto& entry : ring_) {
      ENVOY_LOG(trace, "ring hash: host={} hash={}",
                use_hostname_for_hashing ? entry.host_->hostname()
                                         : entry.host_->address()->asString(),
                entry.hash_);
    }
  }

  stats_.size_.set(ring_size);
  stats_.min_hashes_per_host_.set(min_hashes_per_host);
  stats_.max_hashes_per_host_.set(max_hashes_per_host);
}

} // namespace Upstream
} // namespace Envoy
