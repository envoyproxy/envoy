#include "common/upstream/ring_hash_lb.h"

#include <cstdint>
#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>

#include "common/common/assert.h"
#include "common/common/numeric.h"
#include "common/upstream/load_balancer_impl.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Upstream {

RingHashLoadBalancer::RingHashLoadBalancer(
    const PrioritySet& priority_set, ClusterStats& stats, Runtime::Loader& runtime,
    Runtime::RandomGenerator& random,
    const absl::optional<envoy::api::v2::Cluster::RingHashLbConfig>& config,
    const envoy::api::v2::Cluster::CommonLbConfig& common_config)
    : ThreadAwareLoadBalancerBase(priority_set, stats, runtime, random, common_config),
      config_(config) {}

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
    const absl::optional<envoy::api::v2::Cluster::RingHashLbConfig>& config) {
  ENVOY_LOG(trace, "ring hash: building ring");

  // Sanity-check that the locality weights, if provided, line up with the hosts per locality.
  if (locality_weights != nullptr) {
    ASSERT(locality_weights->size() == hosts_per_locality.get().size());
  }

  // NOTE: Currently we keep a ring for healthy hosts and unhealthy hosts, and this is done per
  //       thread. This is the simplest implementation, but it's expensive from a memory
  //       standpoint and duplicates the regeneration computation. In the future we might want
  //       to generate the rings centrally and then just RCU them out to each thread. This is
  //       sufficient for getting started.

  const uint64_t min_ring_size =
      config ? PROTOBUF_GET_WRAPPED_OR_DEFAULT(config.value(), minimum_ring_size,
                                               DEFAULT_MIN_RING_SIZE)
             : DEFAULT_MIN_RING_SIZE;
  const uint64_t max_ring_size =
      config ? PROTOBUF_GET_WRAPPED_OR_DEFAULT(config.value(), maximum_ring_size,
                                               DEFAULT_MAX_RING_SIZE)
             : DEFAULT_MAX_RING_SIZE;
  const absl::optional<uint64_t> target_hashes_per_host =
      config && config->has_target_hashes_per_host()
          ? absl::optional<uint64_t>(config->target_hashes_per_host().value())
          : absl::nullopt;

  // Sanity-check ring size bounds.
  if (min_ring_size > max_ring_size) {
    ENVOY_LOG(error, "ring hash: minimum_ring_size ({}) > maximum_ring_size ({})", min_ring_size,
              max_ring_size);
    return;
  }

  // Compute the "effective" weight of each host as the product of its own weight and the locality
  // weight, if given. Sum these effective weights and find their greatest common denominator for
  // normalization.
  // NOTE: GCD is associative, same as std::max() for example. To evaluate GCD(a, b, c, ...), we
  //       compute GCD(GCD(GCD(GCD(0, a), b), c), ...).
  uint64_t weighted_sum = 0;
  uint32_t gcd = 0;
  std::unordered_map<HostConstSharedPtr, uint32_t> effective_weights;
  for (uint64_t i = 0; i < hosts_per_locality.get().size(); ++i) {
    for (const auto& host : hosts_per_locality.get()[i]) {
      auto host_weight = host->weight();
      ASSERT(host_weight != 0);
      // NOTE: Locality weight may be zero. In this case, all hosts in the locality are assigned
      //       no load. When we build the ring below, such hosts will have no entries in the ring.
      // TODO: When we move to C++17, change this to `locality_weights[i]` (i.e. use
      //       std::shared_ptr::operator[]) rather than dereferencing locality_weights explicitly.
      auto locality_weight = locality_weights == nullptr ? 1 : (*locality_weights)[i];
      auto effective_weight = host_weight * locality_weight;
      weighted_sum += effective_weight;
      gcd = Envoy::gcd(gcd, effective_weight);
      effective_weights[host] = effective_weight;
    }
  }

  // We can't do anything sensible with no hosts.
  if (weighted_sum == 0) {
    return;
  }
  weighted_sum /= gcd;

  // Determine the valid range of hashes per host. Note that the minimum is rounded up (to meet or
  // exceed the min_ring_size) whereas the maximum is rounded down. If the range is invalid, it
  // indicates unreasonably tight ring size bounds. (We shouldn't attempt any awkward math involving
  // fractional hashes per host).
  const uint64_t min_hashes_per_host =
      (min_ring_size / weighted_sum) + (min_ring_size % weighted_sum != 0);
  const uint64_t max_hashes_per_host = (max_ring_size / weighted_sum);
  if (min_hashes_per_host > max_hashes_per_host) {
    const uint64_t suggested_min_ring_size = max_hashes_per_host * weighted_sum;
    const uint64_t suggested_max_ring_size = min_hashes_per_host * weighted_sum;
    ENVOY_LOG(error,
              "ring hash: ring size bounds are too tight, "
              "decrease minimum to {} or increase maximum to {}",
              suggested_min_ring_size, suggested_max_ring_size);
    return;
  }

  // Determine the actual number of hashes per host within the above bounds, which may differ from
  // the target (if provided), and use that to finally determine the ring size.
  uint64_t hashes_per_host;
  if (target_hashes_per_host) {
    // If a target is provided, try to use it and warn if not possible.
    uint64_t target = target_hashes_per_host.value();
    if (target < min_hashes_per_host) {
      ENVOY_LOG(warn,
                "ring hash: target_hashes_per_host ({}) too small for minimum_ring_size ({})");
      hashes_per_host = min_hashes_per_host;
    } else if (target > max_hashes_per_host) {
      ENVOY_LOG(warn,
                "ring hash: target_hashes_per_host ({}) too large for maximum_ring_size ({})");
      hashes_per_host = max_hashes_per_host;
    } else {
      hashes_per_host = target;
    }
  } else {
    // If a target isn't provided, use the old behavior for backward compatibility: size the ring
    // as small as possible.
    hashes_per_host = min_hashes_per_host;
  }

  const uint64_t ring_size = weighted_sum * hashes_per_host;
  ring_.reserve(ring_size);
  ENVOY_LOG(info, "ring hash: ring_size={} hashes_per_host={}", ring_size, hashes_per_host);

  const bool use_std_hash =
      config ? PROTOBUF_GET_WRAPPED_OR_DEFAULT(config.value().deprecated_v1(), use_std_hash, false)
             : false;

  const HashFunction hash_function =
      config ? config.value().hash_function()
             : HashFunction::Cluster_RingHashLbConfig_HashFunction_XX_HASH;

  char hash_key_buffer[196];
  for (const auto& entry : effective_weights) {
    auto host = entry.first;
    auto effective_weight = entry.second / gcd;
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
    for (uint64_t i = 0; i < effective_weight * hashes_per_host; i++) {
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
