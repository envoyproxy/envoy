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

  const auto* typed_lb_config =
      dynamic_cast<const Upstream::LoadBalancerConfigWrapper*>(lb_config.ptr());
  auto typed_proto_config = typed_lb_config == nullptr
                                ? OptRef<const RingHashLbProto>{}
                                : typed_lb_config->typedProtoConfig<RingHashLbProto>();

  // Assume legacy config.
  if (!typed_proto_config.has_value()) {
    return std::make_unique<Upstream::RingHashLoadBalancer>(
        priority_set, cluster_info.lbStats(), cluster_info.statsScope(), runtime, random,
        cluster_info.lbRingHashConfig(), cluster_info.lbConfig());
  }

  return std::make_unique<Upstream::RingHashLoadBalancer>(
      priority_set, cluster_info.lbStats(), cluster_info.statsScope(), runtime, random,
      PROTOBUF_PERCENT_TO_ROUNDED_INTEGER_OR_DEFAULT(cluster_info.lbConfig(),
                                                     healthy_panic_threshold, 100, 50),
      typed_proto_config.value());
}

/**
 * Static registration for the Factory. @see RegisterFactory.
 */
REGISTER_FACTORY(Factory, Upstream::TypedLoadBalancerFactory);

} // namespace RingHash
} // namespace LoadBalancingPolices
} // namespace Extensions
} // namespace Envoy
