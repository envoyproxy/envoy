#include "source/extensions/load_balancing_policies/ring_hash/config.h"

#include "source/extensions/load_balancing_policies/ring_hash/ring_hash_lb.h"

namespace Envoy {
namespace Extensions {
namespace LoadBalancingPolices {
namespace RingHash {

Upstream::ThreadAwareLoadBalancerPtr
Factory::create(OptRef<const Upstream::LoadBalancerConfig> lb_config,
                const Upstream::ClusterInfo& cluster_info,
                const Upstream::PrioritySet& priority_set, Runtime::Loader& runtime,
                Random::RandomGenerator& random, TimeSource&) {

  auto active_or_legacy =
      Common::ActiveOrLegacy<Upstream::TypedRingHashLbConfig,
                             Upstream::LegacyRingHashLbConfig>::get(lb_config.ptr());

  // Assume legacy config.
  if (!active_or_legacy.hasActive()) {
    return std::make_unique<Upstream::RingHashLoadBalancer>(
        priority_set, cluster_info.lbStats(), cluster_info.statsScope(), runtime, random,
        !active_or_legacy.hasLegacy() ? cluster_info.lbRingHashConfig()
                                      : active_or_legacy.legacy()->lbConfig(),
        cluster_info.lbConfig());
  }

  return std::make_unique<Upstream::RingHashLoadBalancer>(
      priority_set, cluster_info.lbStats(), cluster_info.statsScope(), runtime, random,
      PROTOBUF_PERCENT_TO_ROUNDED_INTEGER_OR_DEFAULT(cluster_info.lbConfig(),
                                                     healthy_panic_threshold, 100, 50),
      active_or_legacy.active()->lb_config_);
}

/**
 * Static registration for the Factory. @see RegisterFactory.
 */
REGISTER_FACTORY(Factory, Upstream::TypedLoadBalancerFactory);

} // namespace RingHash
} // namespace LoadBalancingPolices
} // namespace Extensions
} // namespace Envoy
