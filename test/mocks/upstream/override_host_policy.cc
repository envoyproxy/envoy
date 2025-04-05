#include "test/mocks/upstream/override_host_policy.h"

namespace Envoy {
namespace Upstream {

MockOverrideHostPolicy::MockOverrideHostPolicy() {
  ON_CALL(*this, overrideHostToSelect(testing::_))
      .WillByDefault(testing::Invoke(
          [this](LoadBalancerContext*) -> absl::optional<OverrideHost> { return override_host_; }));
}

} // namespace Upstream
} // namespace Envoy
