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
  NiceMock<Upstream::MockThreadLocalCluster> thread_local_cluster_;
  GenericConnPoolFactory factory_;
  NiceMock<Envoy::Tcp::ConnectionPool::MockUpstreamCallbacks> callbacks_;
};

TEST_F(TcpConnPoolTest, TestNoConnPool) {
  envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy_TunnelingConfig config;
  config.set_hostname("host");
  EXPECT_CALL(thread_local_cluster_, httpConnPool(_, _, _)).WillOnce(Return(nullptr));
  EXPECT_EQ(nullptr,
            factory_.createGenericConnPool(thread_local_cluster_, config, nullptr, callbacks_));
}

} // namespace Generic
} // namespace Tcp
} // namespace Upstreams
} // namespace Extensions
} // namespace Envoy
