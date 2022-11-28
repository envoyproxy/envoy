#include "source/common/upstream/ring_hash_lb.h"

#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "envoy/config/cluster/v3/cluster.pb.h"

#include "source/common/common/assert.h"
#include "source/common/upstream/load_balancer_impl.h"

#include "absl/container/inlined_vector.h"
#include "absl/strings/string_view.h"

namespace Envoy {
namespace Upstream {

Ring::HashFunction RingHashLoadBalancer::toRingHashFunction(const HashFunction& hash_func) const {
  switch (hash_func) {
  case HashFunction::Cluster_RingHashLbConfig_HashFunction_XX_HASH:
    return Ring::HashFunction::XX_HASH;
  case HashFunction::Cluster_RingHashLbConfig_HashFunction_MURMUR_HASH_2:
    return Ring::HashFunction::MURMUR_HASH_2;
  default:
    throw EnvoyException("Unsupported hash function");
  }
}

RingHashLoadBalancer::RingHashLoadBalancer(
    const PrioritySet& priority_set, ClusterLbStats& stats, Stats::Scope& scope,
    Runtime::Loader& runtime, Random::RandomGenerator& random,
    const absl::optional<envoy::config::cluster::v3::Cluster::RingHashLbConfig>& config,
    const envoy::config::cluster::v3::Cluster::CommonLbConfig& common_config)
    : ThreadAwareLoadBalancerBase(priority_set, stats, runtime, random, common_config),
      scope_(scope.createScope("ring_hash_lb.")), stats_(Ring::generateStats(*scope_)),
      min_ring_size_(config ? PROTOBUF_GET_WRAPPED_OR_DEFAULT(config.value(), minimum_ring_size,
                                                              Ring::DefaultMinRingSize)
                            : Ring::DefaultMinRingSize),
      max_ring_size_(config ? PROTOBUF_GET_WRAPPED_OR_DEFAULT(config.value(), maximum_ring_size,
                                                              Ring::DefaultMaxRingSize)
                            : Ring::DefaultMaxRingSize),
      hash_function_(
          toRingHashFunction(config ? config.value().hash_function()
                                    : HashFunction::Cluster_RingHashLbConfig_HashFunction_XX_HASH)),
      use_hostname_for_hashing_(
          common_config.has_consistent_hashing_lb_config()
              ? common_config.consistent_hashing_lb_config().use_hostname_for_hashing()
              : false),
      hash_balance_factor_(PROTOBUF_GET_WRAPPED_OR_DEFAULT(
          common_config.consistent_hashing_lb_config(), hash_balance_factor, 0)) {
  // It's important to do any config validation here, rather than deferring to Ring's ctor,
  // because any exceptions thrown here will be caught and handled properly.
  if (min_ring_size_ > max_ring_size_) {
    throw EnvoyException(fmt::format("ring hash: minimum_ring_size ({}) > maximum_ring_size ({})",
                                     min_ring_size_, max_ring_size_));
  }
}

} // namespace Upstream
} // namespace Envoy
