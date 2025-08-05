#include "source/extensions/load_balancing_policies/wrr_locality/config.h"

// #include "third_party/envoy/src/envoy/registry/registry.h"
// #include "third_party/envoy/src/envoy/upstream/load_balancer.h"

namespace Envoy {
namespace Extensions {
namespace LoadBalancingPolicies {
namespace WrrLocality {

/**
 * Static registration for the Factory. @see RegisterFactory.
 */
REGISTER_FACTORY(Factory, Envoy::Upstream::TypedLoadBalancerFactory);

} // namespace WrrLocality
} // namespace LoadBalancingPolicies
} // namespace Extensions
} // namespace Envoy
