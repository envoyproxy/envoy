#include "load_balancer.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Upstream {
using ::testing::_;
MockLoadBalancer::MockLoadBalancer() {
  ON_CALL(*this, chooseHost(_)).WillByDefault([this] { return HostSelectionResponse{host_}; });
}

MockLoadBalancer::~MockLoadBalancer() = default;

} // namespace Upstream
} // namespace Envoy
