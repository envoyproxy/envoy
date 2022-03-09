#include "test/integration/load_balancers/custom_lb_policy.h"

#include "envoy/registry/registry.h"

namespace Envoy {

REGISTER_FACTORY(CustomLbFactory, Upstream::TypedLoadBalancerFactory);

} // namespace Envoy
