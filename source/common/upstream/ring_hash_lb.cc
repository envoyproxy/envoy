#include "common/upstream/ring_hash_lb.h"

#include <cmath>
#include <cstdint>
#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>

#include "common/common/assert.h"
#include "common/upstream/load_balancer_impl.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Upstream {

RingHashLoadBalancer::RingHashLoadBalancer(
    const PrioritySet& priority_set, ClusterStats& stats, Stats::Scope& scope,
    Runtime::Loader& runtime, Runtime::RandomGenerator& random,
    const absl::optional<envoy::api::v2::Cluster::RingHashLbConfig>& config,
    const envoy::api::v2::Cluster::CommonLbConfig& common_config)
    : ThreadAwareLoadBalancerBase(priority_set, stats, runtime, random, common_config),
      config_(config), scope_(scope.createScope("ring_hash_lb.")), stats_(generateStats(*scope_)) {}

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

using HashFunction = envoy::api::v2::Cluster_RingHashLbConfig_HashFunction;
RingHashLoadBalancer::Ring::Ring(
    const HostsPerLocality& hosts_per_locality,
    const LocalityWeightsConstSharedPtr& locality_weights,
    const absl::optional<envoy::api::v2::Cluster::RingHashLbConfig>& config,
    RingHashLoadBalancerStats& stats)
    : stats_(stats) {
  ENVOY_LOG(trace, "ring hash: building ring");

  // Sanity-check that the locality weights, if provided, line up with the hosts per locality.
  if (locality_weights != nullptr) {
    ASSERT(locality_weights->size() == hosts_per_locality.get().size());
  }

  const uint64_t min_ring_size =
      config
          ? PROTOBUF_GET_WRAPPED_OR_DEFAULT(config.value(), minimum_ring_size, DefaultMinRingSize)
          : DefaultMinRingSize;
  const uint64_t max_ring_size =
      config
          ? PROTOBUF_GET_WRAPPED_OR_DEFAULT(config.value(), maximum_ring_size, DefaultMaxRingSize)
          : DefaultMaxRingSize;
  const absl::optional<uint64_t> configured_replication_factor =
      config && config->has_replication_factor()
          ? absl::optional<uint64_t>(config->replication_factor().value())
          : absl::nullopt;

  // Sanity-check ring size bounds.
  if (min_ring_size > max_ring_size) {
    throw EnvoyException(fmt::format("ring hash: minimum_ring_size ({}) > maximum_ring_size ({})",
                                     min_ring_size, max_ring_size));
  }

  // Compute the "effective" weight of each host as the product of its own weight and the locality
  // weight, if given. Sum these effective weights.
  uint64_t weighted_sum = 0;
  std::unordered_map<HostConstSharedPtr, uint32_t> effective_weights;
  for (uint64_t i = 0; i < hosts_per_locality.get().size(); ++i) {
    for (const auto& host : hosts_per_locality.get()[i]) {
      auto host_weight = host->weight();
      ASSERT(host_weight != 0);
      // NOTE: Locality weight may be explicitly set to zero, meaning any hosts in the locality
      //       should be assigned no load.
      // TODO: When we move to C++17, change this to `locality_weights[i]` (i.e. use
      //       std::shared_ptr::operator[]) rather than dereferencing locality_weights explicitly.
      auto locality_weight = locality_weights == nullptr ? 1 : (*locality_weights)[i];
      if (locality_weight != 0) {
        const auto effective_weight = host_weight * locality_weight;
        weighted_sum += effective_weight;
        effective_weights[host] = effective_weight;
      }
    }
  }

  // We can't do anything sensible with no hosts.
  if (weighted_sum == 0) {
    return;
  }

  // Determine the valid range of replication factors given the weighted sum and ring size bounds,
  // and the actual replication factor within that range. This may differ from the configured
  // replication factor if provided. Note that there's some subtle but important considerations
  // for proper consistent hashing behavior here. First, we find the absolute minimum and maximum
  // replication factors that would fit the configured ring size bounds. These will work out to be
  // weird fractions most of the time (e.g. 123.45 hashes per host), which doesn't make sense, of
  // course, there's no such thing as a fractional entry on the ring...
  double min_replication_factor =
      static_cast<double>(min_ring_size) / static_cast<double>(weighted_sum);
  double max_replication_factor =
      static_cast<double>(max_ring_size) / static_cast<double>(weighted_sum);

  // ... So, we find the closest whole number replication factors by rounding up the minimum and
  // rounding down the maximum. These will be good most of the time, but there is still an edge
  // case: suppose the user sets minimum_ring_size and maximum_ring_size both to exactly the same
  // number, such as 1024 entries (which is something they should probably be allowed to do), and
  // then has a weighted_sum of hosts that doesn't divide evenly into 1024, such as 5. The exact
  // fractional replication factor computed above would be 204.8. So here we'd say that the
  // min_whole_replication_factor would be 205 and the max would be 204, which is no good. Our
  // policy is to try and use whole number replication factors when possible, and only use
  // fractional replication factors when we absolutely have to.
  const double min_whole_replication_factor = std::ceil(min_replication_factor);
  const double max_whole_replication_factor = std::floor(max_replication_factor);
  if (min_whole_replication_factor <= max_whole_replication_factor) {
    min_replication_factor = min_whole_replication_factor;
    max_replication_factor = max_whole_replication_factor;
  }

  // Determine the actual replication factor we will use. If the configuration includes a
  // replication factor, try to use it (or whatever value is closest to it within bounds).
  // Otherwise, fall back to the old behavior for backward compatibility: size the ring as small as
  // possible.
  const double replication_factor =
      configured_replication_factor
          ? std::min(std::max(static_cast<double>(configured_replication_factor.value()),
                              min_replication_factor),
                     max_replication_factor)
          : min_replication_factor;

  // Reserve memory for the entire ring up front.
  const uint64_t ring_size = std::ceil(weighted_sum * replication_factor);
  ring_.reserve(ring_size);
  stats_.size_.set(ring_size);
  stats_.replication_factor_.set(replication_factor);

  const bool use_std_hash =
      config ? PROTOBUF_GET_WRAPPED_OR_DEFAULT(config.value().deprecated_v1(), use_std_hash, false)
             : false;

  const HashFunction hash_function =
      config ? config.value().hash_function()
             : HashFunction::Cluster_RingHashLbConfig_HashFunction_XX_HASH;

  // Populate the hash ring by walking through the (host, weight) entries in the effective_weights
  // map, and generating (replication_factor * weight) hashes for each host. This would be
  // straightforward if we always had integer replication factors, but if the replication factor
  // is fractional (for reasons described above), it gets slightly more complicated. We maintain
  // running sums -- current_replicas and target_replicas -- which allows us to handle fractional
  // replication_factors in a somewhat stable way.
  //
  // For example, suppose we have N hosts, each with a weight of 1, and a replication factor of 1.5
  // hashes per host. We start the outer loop with current_replicas = 0 and target_replicas = 0.
  //   - For the first host, we set target_replicas = 1.5. After one run of the inner loop,
  //     current_replicas = 1. After another run, current_replicas = 2, so the inner loop ends.
  //   - For the second host, target_replicas becomes 3.0, and current_replicas is 2 from before.
  //     After only one run of the inner loop, current_replicas = 3, so the inner loop ends.
  // This continues with all even-numbered hosts getting two entries in the ring, and all odd-
  // numbered hosts getting one entry, which averages out to 1.5 hashes per host. This math still
  // works in the extreme case where the replication_factor is less than 1 (that is, there are more
  // weighted hosts than capacity in the ring): some hosts will get no entries in the ring.
  //
  // Aside from trying to maintain an average replication factor strictly within the ring size
  // bounds, the other goal of this algorithm is to provide fair consistent hashing behavior even
  // when non-integer replication factors are used. For example, suppose the user specifies a fixed
  // ring size, min=max=1024, and there are initially 512 hosts in the ring, giving a replication
  // factor of 2. If one host becomes unhealthy, the replication factor becomes ~2.004; two of the
  // remaining 511 healthy hosts will end up with three replicas, and all the others will still have
  // two as before.
  //
  // Having said all this, it's almost always vastly preferable for the user to avoid this situation
  // and simply specify a fixed replication factor, with ring size bounds that can accommodate some
  // variation in the number of hosts.
  char hash_key_buffer[196];
  double current_replicas = 0.0;
  double target_replicas = 0.0;
  for (const auto& entry : effective_weights) {
    const auto& host = entry.first;
    const std::string& address_string = host->address()->asString();
    uint64_t offset_start = address_string.size();

    // Currently, we support both IP and UDS addresses. The UDS max path length is ~108 on all Unix
    // platforms that I know of. Given that, we can use a 196 char buffer which is plenty of room
    // for UDS, '_', and up to 21 characters for the node ID. To be on the super safe side, there
    // is a RELEASE_ASSERT here that checks this, in case someone in the future adds some type of
    // new address that is larger, or runs on a platform where UDS is larger. I don't think it's
    // worth the defensive coding to deal with the heap allocation case (e.g. via
    // absl::InlinedVector) at the current time.
    RELEASE_ASSERT(
        address_string.size() + 1 + StringUtil::MIN_ITOA_OUT_LEN <= sizeof(hash_key_buffer), "");
    memcpy(hash_key_buffer, address_string.c_str(), offset_start);
    hash_key_buffer[offset_start++] = '_';

    // Inner loop as noted above: maintain current_replicas and target_replicas as running sums
    // across the entire host set. `i` is needed only to construct the hash key.
    target_replicas += replication_factor * entry.second;
    uint64_t i = 0;
    while (current_replicas < target_replicas) {
      const uint64_t total_hash_key_len =
          offset_start +
          StringUtil::itoa(hash_key_buffer + offset_start, StringUtil::MIN_ITOA_OUT_LEN, i);
      absl::string_view hash_key(hash_key_buffer, total_hash_key_len);

      // Sadly std::hash provides no mechanism for hashing arbitrary bytes so we must copy here.
      // xxHash is done without copies.
      const uint64_t hash =
          use_std_hash
              ? std::hash<std::string>()(std::string(hash_key))
              : (hash_function == HashFunction::Cluster_RingHashLbConfig_HashFunction_MURMUR_HASH_2)
                    ? MurmurHash::murmurHash2_64(hash_key, MurmurHash::STD_HASH_SEED)
                    : HashUtil::xxHash64(hash_key);

      ENVOY_LOG(trace, "ring hash: hash_key={} hash={}", hash_key.data(), hash);
      ring_.push_back({hash, host});
      ++i;
      ++current_replicas;
    }
  }

  std::sort(ring_.begin(), ring_.end(), [](const RingEntry& lhs, const RingEntry& rhs) -> bool {
    return lhs.hash_ < rhs.hash_;
  });
  if (ENVOY_LOG_CHECK_LEVEL(trace)) {
    for (const auto& entry : ring_) {
      ENVOY_LOG(trace, "ring hash: host={} hash={}", entry.host_->address()->asString(),
                entry.hash_);
    }
  }
}

} // namespace Upstream
} // namespace Envoy
