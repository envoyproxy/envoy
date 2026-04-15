#include "source/extensions/load_balancing_policies/original_dst/config.h"

namespace Envoy {
namespace Extensions {
namespace LoadBalancingPolicies {
namespace OriginalDst {

Upstream::ThreadAwareLoadBalancerPtr Factory::create(OptRef<const Upstream::LoadBalancerConfig>,
                                                     const Upstream::ClusterInfo&,
                                                     const Upstream::PrioritySet&, Runtime::Loader&,
                                                     Random::RandomGenerator&, TimeSource&) {
  // Original destination load balancer is provided by the original_dst cluster itself.
  // The cluster creates its own ThreadAwareLoadBalancer that has access to the cluster's
  // host map and can add hosts on demand based on the original destination address.
  return nullptr;
}

/**
 * Static registration for the Factory. @see RegisterFactory.
 */
REGISTER_FACTORY(Factory, Upstream::TypedLoadBalancerFactory);

} // namespace OriginalDst
} // namespace LoadBalancingPolicies
} // namespace Extensions
} // namespace Envoy
