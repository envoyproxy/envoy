#include "load_balancer.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Upstream {
using ::testing::_;
using ::testing::Invoke;
MockLoadBalancer::MockLoadBalancer() {
  ON_CALL(*this, chooseHost(_)).WillByDefault(Invoke([this] {
    return HostSelectionResponse{host_};
  }));
}

MockLoadBalancer::~MockLoadBalancer() = default;

MockConnectionStateProvider::MockConnectionStateProvider() = default;
MockConnectionStateProvider::~MockConnectionStateProvider() = default;

} // namespace Upstream
} // namespace Envoy
