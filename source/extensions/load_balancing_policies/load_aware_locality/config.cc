#include "source/extensions/load_balancing_policies/load_aware_locality/config.h"

namespace Envoy {
namespace Extensions {
namespace LoadBalancingPolicies {
namespace LoadAwareLocality {

Upstream::ThreadAwareLoadBalancerPtr Factory::create(OptRef<const Upstream::LoadBalancerConfig>,
                                                     const Upstream::ClusterInfo&,
                                                     const Upstream::PrioritySet&, Runtime::Loader&,
                                                     Envoy::Random::RandomGenerator&, TimeSource&) {
  // TODO(jukie): Implement load-aware locality load balancer.
  return nullptr;
}

absl::StatusOr<Upstream::LoadBalancerConfigPtr>
Factory::loadConfig(Server::Configuration::ServerFactoryContext&, const Protobuf::Message&) {
  // TODO(jukie): Implement load-aware locality config loading.
  return absl::UnimplementedError(
      "envoy.load_balancing_policies.load_aware_locality is not yet implemented");
}

REGISTER_FACTORY(Factory, Upstream::TypedLoadBalancerFactory);

} // namespace LoadAwareLocality
} // namespace LoadBalancingPolicies
} // namespace Extensions
} // namespace Envoy
