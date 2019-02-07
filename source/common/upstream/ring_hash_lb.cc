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
      // NOTE: Locality weight may be zero. In this case, all hosts in the locality are assigned
      //       no load. When we build the ring below, such hosts will have no entries in the ring.
      // TODO: When we move to C++17, change this to `locality_weights[i]` (i.e. use
      //       std::shared_ptr::operator[]) rather than dereferencing locality_weights explicitly.
      auto locality_weight = locality_weights == nullptr ? 1 : (*locality_weights)[i];
      auto effective_weight = host_weight * locality_weight;
      weighted_sum += effective_weight;
      effective_weights[host] = effective_weight;
    }
  }

  // We can't do anything sensible with no hosts.
  if (weighted_sum == 0) {
    return;
  }

  // Determine the valid range of replication factors given the weighted sum and ring size bounds,
  // and the actual replication factor within that range. This may differ from the configured
  // replication factor if provided. Note that we use floating point math here and round up when
  // adding hosts to the ring, rather than trying to preserve exact integer ratios. This is good
  // enough, since the distribution of hosts around the ring wouldn't exactly match the given
  // weights anyway, even with a good hash function.
  const double min_replication_factor =
      static_cast<double>(min_ring_size) / static_cast<double>(weighted_sum);
  const double max_replication_factor =
      static_cast<double>(max_ring_size) / static_cast<double>(weighted_sum);
  double replication_factor;
  if (configured_replication_factor) {
    // If the configuration includes a replication factor, try to use it and warn if not possible.
    replication_factor = static_cast<double>(configured_replication_factor.value());
    if (replication_factor < min_replication_factor) {
      ENVOY_LOG(warn, "ring hash: replication_factor ({}) too small for minimum_ring_size ({})",
                replication_factor, min_ring_size);
      replication_factor = min_replication_factor;
    } else if (replication_factor > max_replication_factor) {
      ENVOY_LOG(warn, "ring hash: replication_factor ({}) too large for maximum_ring_size ({})",
                replication_factor, max_ring_size);
      replication_factor = max_replication_factor;
    }
  } else {
    // If a replication factor isn't provided, use the old behavior for backward compatibility: size
    // the ring as small as possible while maintaining integer weights.
    replication_factor = std::ceil(min_replication_factor);
  }

  // Adjust the ring entry weights by multiplying the entry weights with the replication factor,
  // rounding up to avoid losing entries with weights less than 1, and sum these to determine the
  // actual ring size.
  // NOTE: This rounding changes the effective replication factor from what was determined above.
  //       In some contrived cases, we could actually exceed the configured max ring size, but this
  //       is probably better behavior than rounding down and losing entries.
  uint64_t ring_size = 0;
  for (auto& entry : effective_weights) {
    entry.second = static_cast<uint64_t>(std::ceil(entry.second * replication_factor));
    ring_size += entry.second;
  }
  ring_.reserve(ring_size);
  ENVOY_LOG(info, "ring hash: ring_size={} replication_factor={}", ring_size,
            static_cast<double>(ring_size) / static_cast<double>(weighted_sum));

  const bool use_std_hash =
      config ? PROTOBUF_GET_WRAPPED_OR_DEFAULT(config.value().deprecated_v1(), use_std_hash, false)
             : false;

  const HashFunction hash_function =
      config ? config.value().hash_function()
             : HashFunction::Cluster_RingHashLbConfig_HashFunction_XX_HASH;

  char hash_key_buffer[196];
  for (const auto& entry : effective_weights) {
    auto host = entry.first;
    auto replicas = entry.second;
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
    for (uint64_t i = 0; i < replicas; i++) {
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
