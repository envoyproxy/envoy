#include "common/tcp_proxy/tcp_proxy.h"

#include "extensions/filters/network/sni_cluster/config.h"
#include "extensions/filters/network/sni_cluster/sni_cluster.h"

#include "test/mocks/network/mocks.h"
#include "test/mocks/server/mocks.h"
#include "test/mocks/stream_info/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::Matcher;
using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace SniCluster {

class SniClusterFilterTest : public testing::Test {
public:
  SniClusterFilterTest() {
    ON_CALL(filter_callbacks_.connection_, streamInfo()).WillByDefault(ReturnRef(stream_info_));
    ON_CALL(Const(filter_callbacks_.connection_), streamInfo())
        .WillByDefault(ReturnRef(stream_info_));
    configure(envoy::config::filter::network::sni_cluster::v2::SniCluster());
  }

  void configure(envoy::config::filter::network::sni_cluster::v2::SniCluster proto_config) {
    config_ = std::make_unique<SniClusterFilterConfig>(proto_config);
    filter_ = std::make_unique<SniClusterFilter>(config_);
    filter_->initializeReadFilterCallbacks(filter_callbacks_);
  }

  NiceMock<Network::MockReadFilterCallbacks> filter_callbacks_;
  NiceMock<StreamInfo::MockStreamInfo> stream_info_;
  SniClusterFilterConfigSharedPtr config_;
  std::unique_ptr<SniClusterFilter> filter_;
};

// Test that per connection filter config is set if SNI is available
TEST_F(SniClusterFilterTest, SetTcpProxyClusterOnlyIfSniIsPresent) {
  // no sni
  {
    ON_CALL(filter_callbacks_.connection_, requestedServerName())
        .WillByDefault(Return(EMPTY_STRING));
    filter_->onNewConnection();

    EXPECT_FALSE(stream_info_.filterState().hasData<TcpProxy::PerConnectionCluster>(
        TcpProxy::PerConnectionCluster::Key));
  }

  // with sni
  {
    ON_CALL(filter_callbacks_.connection_, requestedServerName())
        .WillByDefault(Return("filter_state_cluster"));
    filter_->onNewConnection();

    EXPECT_TRUE(stream_info_.filterState().hasData<TcpProxy::PerConnectionCluster>(
        TcpProxy::PerConnectionCluster::Key));

    auto per_connection_cluster =
        stream_info_.filterState().getDataReadOnly<TcpProxy::PerConnectionCluster>(
            TcpProxy::PerConnectionCluster::Key);
    EXPECT_EQ(per_connection_cluster.value(), "filter_state_cluster");
  }
}

TEST_F(SniClusterFilterTest, SniSubstitution) {
  // no substitution
  {
    ON_CALL(filter_callbacks_.connection_, requestedServerName())
        .WillByDefault(Return("hello.ns1.svc.cluster.local"));
    filter_->onNewConnection();

    EXPECT_TRUE(stream_info_.filterState().hasData<TcpProxy::PerConnectionCluster>(
        TcpProxy::PerConnectionCluster::Key));

    auto per_connection_cluster =
        stream_info_.filterState().getDataReadOnly<TcpProxy::PerConnectionCluster>(
            TcpProxy::PerConnectionCluster::Key);
    EXPECT_EQ(per_connection_cluster.value(), "hello.ns1.svc.cluster.local");
  }

  // with simple substitution
  {
    envoy::config::filter::network::sni_cluster::v2::SniCluster proto_config;
    auto* sni_substitution = proto_config.mutable_sni_substitution();
    sni_substitution->set_regex("\\.global$");
    sni_substitution->set_replacement(".svc.cluster.local");
    configure(proto_config);

    ON_CALL(filter_callbacks_.connection_, requestedServerName())
        .WillByDefault(Return("hello.ns1.global"));
    filter_->onNewConnection();

    EXPECT_TRUE(stream_info_.filterState().hasData<TcpProxy::PerConnectionCluster>(
        TcpProxy::PerConnectionCluster::Key));

    auto per_connection_cluster =
        stream_info_.filterState().getDataReadOnly<TcpProxy::PerConnectionCluster>(
            TcpProxy::PerConnectionCluster::Key);
    EXPECT_EQ(per_connection_cluster.value(), "hello.ns1.svc.cluster.local");
  }

  // with regex substitution
  {
    envoy::config::filter::network::sni_cluster::v2::SniCluster proto_config;
    auto* sni_substitution = proto_config.mutable_sni_substitution();
    sni_substitution->set_regex("^.*$");
    sni_substitution->set_replacement("another.svc.cluster.local");
    configure(proto_config);

    ON_CALL(filter_callbacks_.connection_, requestedServerName())
        .WillByDefault(Return("hello.ns1.global"));
    filter_->onNewConnection();

    EXPECT_TRUE(stream_info_.filterState().hasData<TcpProxy::PerConnectionCluster>(
        TcpProxy::PerConnectionCluster::Key));

    auto per_connection_cluster =
        stream_info_.filterState().getDataReadOnly<TcpProxy::PerConnectionCluster>(
            TcpProxy::PerConnectionCluster::Key);
    EXPECT_EQ(per_connection_cluster.value(), "another.svc.cluster.local");
  }
}

} // namespace SniCluster
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
