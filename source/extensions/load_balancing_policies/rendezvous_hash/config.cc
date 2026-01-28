#include "source/extensions/load_balancing_policies/rendezvous_hash/config.h"

#include "source/extensions/load_balancing_policies/rendezvous_hash/rendezvous_hash_lb.h"

namespace Envoy {
namespace Extensions {
namespace LoadBalancingPolicies {
namespace RendezvousHash {

Upstream::ThreadAwareLoadBalancerPtr
Factory::create(OptRef<const Upstream::LoadBalancerConfig> lb_config,
                const Upstream::ClusterInfo& cluster_info,
                const Upstream::PrioritySet& priority_set, Runtime::Loader& runtime,
                Random::RandomGenerator& random, TimeSource&) {

  const auto typed_lb_config =
      dynamic_cast<const Upstream::TypedRendezvousHashLbConfig*>(lb_config.ptr());
  ASSERT(typed_lb_config != nullptr, "Invalid rendezvous hash load balancer config");

  return std::make_unique<Upstream::RendezvousHashLoadBalancer>(
      priority_set, cluster_info.lbStats(), cluster_info.statsScope(), runtime, random,
      PROTOBUF_PERCENT_TO_ROUNDED_INTEGER_OR_DEFAULT(cluster_info.lbConfig(),
                                                     healthy_panic_threshold, 100, 50),
      typed_lb_config->lb_config_, typed_lb_config->hash_policy_);
}

/**
 * Static registration for the Factory. @see RegisterFactory.
 */
REGISTER_FACTORY(Factory, Upstream::TypedLoadBalancerFactory);

} // namespace RendezvousHash
} // namespace LoadBalancingPolicies
} // namespace Extensions
} // namespace Envoy
