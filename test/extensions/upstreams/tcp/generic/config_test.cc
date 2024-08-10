#include "envoy/extensions/filters/network/tcp_proxy/v3/tcp_proxy.pb.h"

#include "source/common/stream_info/bool_accessor_impl.h"
#include "source/common/tcp_proxy/tcp_proxy.h"
#include "source/extensions/upstreams/tcp/generic/config.h"

#include "test/mocks/http/mocks.h"
#include "test/mocks/server/factory_context.h"
#include "test/mocks/tcp/mocks.h"
#include "test/mocks/upstream/cluster_manager.h"
#include "test/mocks/upstream/load_balancer_context.h"
#include "test/test_common/test_runtime.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::AnyNumber;
using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;

namespace Envoy {
namespace Extensions {
namespace Upstreams {
namespace Tcp {
namespace Generic {
class TcpConnPoolTest : public testing::TestWithParam<bool> {
public:
  TcpConnPoolTest() {
    EXPECT_CALL(lb_context_, downstreamConnection()).WillRepeatedly(Return(&connection_));
  }

  void SetUp() override {
    scoped_runtime_.mergeValues({{"envoy.restart_features.upstream_http_filters_with_tcp_proxy",
                                  GetParam() ? "true" : "false"}});
  }

  bool useUpstreamFilters() { return GetParam(); }

  TestScopedRuntime scoped_runtime_;
  NiceMock<Upstream::MockThreadLocalCluster> thread_local_cluster_;
  GenericConnPoolFactory factory_;
  NiceMock<Envoy::Tcp::ConnectionPool::MockUpstreamCallbacks> callbacks_;
  NiceMock<StreamInfo::MockStreamInfo> downstream_stream_info_;
  NiceMock<Network::MockConnection> connection_;
  Upstream::MockLoadBalancerContext lb_context_;
  envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy tcp_proxy_;
  NiceMock<Stats::MockStore> store_;
  Stats::MockScope& scope_{store_.mockScope()};
  NiceMock<Envoy::Http::MockStreamDecoderFilterCallbacks> decoder_callbacks_;
  NiceMock<Server::Configuration::MockFactoryContext> context_;
};

INSTANTIATE_TEST_SUITE_P(UpstreamHttpFiltersWithTunneling, TcpConnPoolTest,
                         ::testing::Values(true, false));

TEST_P(TcpConnPoolTest, TestNoTunnelingConfig) {
  EXPECT_CALL(thread_local_cluster_, tcpConnPool(_, _)).WillOnce(Return(absl::nullopt));
  EXPECT_EQ(nullptr, factory_.createGenericConnPool(
                         thread_local_cluster_, TcpProxy::TunnelingConfigHelperOptConstRef(),
                         &lb_context_, callbacks_, decoder_callbacks_, downstream_stream_info_));
}

TEST_P(TcpConnPoolTest, TestTunnelingDisabledByFilterState) {
  envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy_TunnelingConfig config_proto;
  tcp_proxy_.mutable_tunneling_config()->set_hostname("host");
  const TcpProxy::TunnelingConfigHelperImpl config(scope_, tcp_proxy_, context_);

  downstream_stream_info_.filterState()->setData(
      TcpProxy::DisableTunnelingFilterStateKey,
      std::make_shared<StreamInfo::BoolAccessorImpl>(true),
      StreamInfo::FilterState::StateType::Mutable, StreamInfo::FilterState::LifeSpan::Connection);

  EXPECT_CALL(thread_local_cluster_, tcpConnPool(_, _)).WillOnce(Return(absl::nullopt));
  EXPECT_EQ(nullptr, factory_.createGenericConnPool(
                         thread_local_cluster_, TcpProxy::TunnelingConfigHelperOptConstRef(config),
                         &lb_context_, callbacks_, decoder_callbacks_, downstream_stream_info_));
}

TEST_P(TcpConnPoolTest, TestTunnelingNotDisabledIfFilterStateHasFalseValue) {
  envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy_TunnelingConfig config_proto;
  tcp_proxy_.mutable_tunneling_config()->set_hostname("host");
  const TcpProxy::TunnelingConfigHelperImpl config(scope_, tcp_proxy_, context_);

  downstream_stream_info_.filterState()->setData(
      TcpProxy::DisableTunnelingFilterStateKey,
      std::make_shared<StreamInfo::BoolAccessorImpl>(false),
      StreamInfo::FilterState::StateType::Mutable, StreamInfo::FilterState::LifeSpan::Connection);

  if (!useUpstreamFilters()) {
    EXPECT_CALL(thread_local_cluster_, httpConnPool(_, _, _)).WillOnce(Return(absl::nullopt));
  }

  EXPECT_EQ(nullptr, factory_.createGenericConnPool(
                         thread_local_cluster_, TcpProxy::TunnelingConfigHelperOptConstRef(config),
                         &lb_context_, callbacks_, decoder_callbacks_, downstream_stream_info_));
}

TEST_P(TcpConnPoolTest, TestNoConnPool) {
  envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy_TunnelingConfig config_proto;
  tcp_proxy_.mutable_tunneling_config()->set_hostname("host");
  const TcpProxy::TunnelingConfigHelperImpl config(scope_, tcp_proxy_, context_);
  if (!useUpstreamFilters()) {
    EXPECT_CALL(thread_local_cluster_, httpConnPool(_, _, _)).WillOnce(Return(absl::nullopt));
  }
  EXPECT_EQ(nullptr, factory_.createGenericConnPool(
                         thread_local_cluster_, TcpProxy::TunnelingConfigHelperOptConstRef(config),
                         &lb_context_, callbacks_, decoder_callbacks_, downstream_stream_info_));
}

TEST_P(TcpConnPoolTest, Http2Config) {
  auto info = std::make_shared<Upstream::MockClusterInfo>();
  const std::string fake_cluster_name = "fake_cluster";

  envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy_TunnelingConfig config_proto;
  tcp_proxy_.mutable_tunneling_config()->set_hostname("host");
  const TcpProxy::TunnelingConfigHelperImpl config(scope_, tcp_proxy_, context_);
  if (!useUpstreamFilters()) {
    EXPECT_CALL(thread_local_cluster_, httpConnPool(_, _, _)).WillOnce(Return(absl::nullopt));
  }
  EXPECT_EQ(nullptr, factory_.createGenericConnPool(
                         thread_local_cluster_, TcpProxy::TunnelingConfigHelperOptConstRef(config),
                         &lb_context_, callbacks_, decoder_callbacks_, downstream_stream_info_));
}

TEST_P(TcpConnPoolTest, Http3Config) {
  auto info = std::make_shared<Upstream::MockClusterInfo>();
  const std::string fake_cluster_name = "fake_cluster";
  EXPECT_CALL(*info, features())
      .Times(AnyNumber())
      .WillRepeatedly(Return(Upstream::ClusterInfo::Features::HTTP3));
  EXPECT_CALL(thread_local_cluster_, info).Times(AnyNumber()).WillRepeatedly(Return(info));
  envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy_TunnelingConfig config_proto;
  tcp_proxy_.mutable_tunneling_config()->set_hostname("host");
  const TcpProxy::TunnelingConfigHelperImpl config(scope_, tcp_proxy_, context_);
  if (!useUpstreamFilters()) {
    EXPECT_CALL(thread_local_cluster_, httpConnPool(_, _, _)).WillOnce(Return(absl::nullopt));
  }
  EXPECT_EQ(nullptr, factory_.createGenericConnPool(
                         thread_local_cluster_, TcpProxy::TunnelingConfigHelperOptConstRef(config),
                         &lb_context_, callbacks_, decoder_callbacks_, downstream_stream_info_));
}

TEST(DisableTunnelingObjectFactory, CreateFromBytes) {
  auto* factory = Registry::FactoryRegistry<StreamInfo::FilterState::ObjectFactory>::getFactory(
      TcpProxy::DisableTunnelingFilterStateKey);
  ASSERT_NE(nullptr, factory);
  auto object = factory->createFromBytes("true");
  ASSERT_NE(nullptr, object);
  EXPECT_EQ(true, dynamic_cast<const StreamInfo::BoolAccessor*>(object.get())->value());
}

} // namespace Generic
} // namespace Tcp
} // namespace Upstreams
} // namespace Extensions
} // namespace Envoy
