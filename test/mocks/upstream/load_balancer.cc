#include "load_balancer.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Upstream {
using ::testing::_;
using ::testing::Return;
MockLoadBalancer::MockLoadBalancer() { ON_CALL(*this, chooseHost(_)).WillByDefault(Return(host_)); }

MockLoadBalancer::~MockLoadBalancer() = default;

} // namespace Upstream
} // namespace Envoy
