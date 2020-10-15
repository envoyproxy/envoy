#include "extensions/upstreams/tcp/generic/config.h"

#include "test/mocks/tcp/mocks.h"
#include "test/mocks/upstream/cluster_manager.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::NiceMock;
using testing::Return;

namespace Envoy {
namespace Extensions {
namespace Upstreams {
namespace Tcp {
namespace Generic {

class TcpConnPoolTest : public ::testing::Test {
public:
  TcpConnPoolTest() {}

  NiceMock<Upstream::MockClusterManager> cluster_manager_;
  const std::string cluster_name_{"cluster_name"};
  GenericConnPoolFactory factory_;
  NiceMock<Envoy::Tcp::ConnectionPool::MockUpstreamCallbacks> callbacks_;
};

TEST_F(TcpConnPoolTest, TestNoMatchingClusterName) {
  EXPECT_CALL(cluster_manager_, get(_)).WillOnce(Return(nullptr));
  EXPECT_EQ(nullptr, factory_.createGenericConnPool(cluster_name_, cluster_manager_,
                                                    absl::optional(std::string("host")), nullptr,
                                                    callbacks_));
}

} // namespace Generic
} // namespace Tcp
} // namespace Upstreams
} // namespace Extensions
} // namespace Envoy
