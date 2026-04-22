#include "source/extensions/load_balancing_policies/wrr_locality/config.h"

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
