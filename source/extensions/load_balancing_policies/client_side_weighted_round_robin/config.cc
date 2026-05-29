#include "source/extensions/load_balancing_policies/client_side_weighted_round_robin/config.h"

#include "source/extensions/load_balancing_policies/client_side_weighted_round_robin/client_side_weighted_round_robin_lb.h"

namespace Envoy {
namespace Extensions {
namespace LoadBalancingPolicies {
namespace ClientSideWeightedRoundRobin {

/**
 * Static registration for the Factory. @see RegisterFactory.
 */
REGISTER_FACTORY(Factory, Upstream::TypedLoadBalancerFactory);

} // namespace ClientSideWeightedRoundRobin
} // namespace LoadBalancingPolicies
} // namespace Extensions
} // namespace Envoy
