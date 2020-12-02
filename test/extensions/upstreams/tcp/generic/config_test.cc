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
  NiceMock<Upstream::MockClusterManager> cluster_manager_;
  const std::string cluster_name_{"cluster_name"};
  GenericConnPoolFactory factory_;
  NiceMock<Envoy::Tcp::ConnectionPool::MockUpstreamCallbacks> callbacks_;
};

TEST_F(TcpConnPoolTest, TestNoMatchingClusterName) {
  envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy_TunnelingConfig config;
  config.set_hostname("host");
  EXPECT_CALL(cluster_manager_, getThreadLocalCluster(_)).WillOnce(Return(nullptr));
  EXPECT_EQ(nullptr, factory_.createGenericConnPool(cluster_name_, cluster_manager_, config,
                                                    nullptr, callbacks_));
}

} // namespace Generic
} // namespace Tcp
} // namespace Upstreams
} // namespace Extensions
} // namespace Envoy
