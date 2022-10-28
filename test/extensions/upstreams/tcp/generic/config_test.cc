#include "source/common/tcp_proxy/tcp_proxy.h"
#include "source/extensions/upstreams/tcp/generic/config.h"

#include "test/mocks/server/factory_context.h"
#include "test/mocks/tcp/mocks.h"
#include "test/mocks/upstream/cluster_manager.h"
#include "test/mocks/upstream/load_balancer_context.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::AnyNumber;
using testing::NiceMock;
using testing::Return;

namespace Envoy {
namespace Extensions {
namespace Upstreams {
namespace Tcp {
namespace Generic {

class TcpConnPoolTest : public ::testing::Test {
public:
  TcpConnPoolTest() {
    EXPECT_CALL(lb_context_, downstreamConnection()).WillRepeatedly(Return(&connection_));
  }
  NiceMock<Upstream::MockThreadLocalCluster> thread_local_cluster_;
  GenericConnPoolFactory factory_;
  NiceMock<Envoy::Tcp::ConnectionPool::MockUpstreamCallbacks> callbacks_;
  NiceMock<StreamInfo::MockStreamInfo> downstream_stream_info_;
  NiceMock<Network::MockConnection> connection_;
  Upstream::MockLoadBalancerContext lb_context_;
  NiceMock<Server::Configuration::MockFactoryContext> context_;
};

TEST_F(TcpConnPoolTest, TestNoConnPool) {
  envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy_TunnelingConfig config_proto;
  config_proto.set_hostname("host");
  const TcpProxy::TunnelingConfigHelperImpl config(config_proto, context_);
  EXPECT_CALL(thread_local_cluster_, httpConnPool(_, _, _)).WillOnce(Return(absl::nullopt));
  EXPECT_EQ(nullptr, factory_.createGenericConnPool(
                         thread_local_cluster_, TcpProxy::TunnelingConfigHelperOptConstRef(config),
                         &lb_context_, callbacks_, downstream_stream_info_));
}

TEST_F(TcpConnPoolTest, Http2Config) {
  auto info = std::make_shared<Upstream::MockClusterInfo>();
  EXPECT_CALL(*info, features()).WillOnce(Return(Upstream::ClusterInfo::Features::HTTP2));
  EXPECT_CALL(thread_local_cluster_, info).WillOnce(Return(info));
  envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy_TunnelingConfig config_proto;
  config_proto.set_hostname("host");
  const TcpProxy::TunnelingConfigHelperImpl config(config_proto, context_);

  EXPECT_CALL(thread_local_cluster_, httpConnPool(_, _, _)).WillOnce(Return(absl::nullopt));
  EXPECT_EQ(nullptr, factory_.createGenericConnPool(
                         thread_local_cluster_, TcpProxy::TunnelingConfigHelperOptConstRef(config),
                         &lb_context_, callbacks_, downstream_stream_info_));
}

TEST_F(TcpConnPoolTest, Http3Config) {
  auto info = std::make_shared<Upstream::MockClusterInfo>();
  EXPECT_CALL(*info, features())
      .Times(AnyNumber())
      .WillRepeatedly(Return(Upstream::ClusterInfo::Features::HTTP3));
  EXPECT_CALL(thread_local_cluster_, info).Times(AnyNumber()).WillRepeatedly(Return(info));
  envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy_TunnelingConfig config_proto;
  config_proto.set_hostname("host");
  const TcpProxy::TunnelingConfigHelperImpl config(config_proto, context_);
  EXPECT_CALL(thread_local_cluster_, httpConnPool(_, _, _)).WillOnce(Return(absl::nullopt));
  EXPECT_EQ(nullptr, factory_.createGenericConnPool(
                         thread_local_cluster_, TcpProxy::TunnelingConfigHelperOptConstRef(config),
                         &lb_context_, callbacks_, downstream_stream_info_));
}

} // namespace Generic
} // namespace Tcp
} // namespace Upstreams
} // namespace Extensions
} // namespace Envoy
