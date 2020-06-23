#include "test/mocks/upstream/load_balancer_context.h"

using testing::_;
using testing::ReturnRef;

namespace Envoy {
namespace Upstream {

MockLoadBalancerContext::MockLoadBalancerContext() {
  // By default, set loads which treat everything as healthy in the first priority.
  priority_load_.healthy_priority_load_ = HealthyLoad({100});
  priority_load_.degraded_priority_load_ = DegradedLoad({0});
  ON_CALL(*this, determinePriorityLoad(_, _, _)).WillByDefault(ReturnRef(priority_load_));
}

MockLoadBalancerContext::~MockLoadBalancerContext() = default;

} // namespace Upstream
} // namespace Envoy
