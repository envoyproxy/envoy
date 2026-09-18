#include "test/extensions/filters/http/ext_authz/callout_state_test_lb.h"

#include "envoy/registry/registry.h"

namespace Envoy {

REGISTER_FACTORY(CalloutStateLbFactory, Upstream::TypedLoadBalancerFactory);

} // namespace Envoy
