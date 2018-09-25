#include "common/tcp_proxy/tcp_proxy.h"

#include "extensions/filters/network/sni_cluster/config.h"
#include "extensions/filters/network/sni_cluster/sni_cluster.h"

#include "test/mocks/network/mocks.h"
#include "test/mocks/server/mocks.h"

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

// Test that a SniCluster filter config works.
TEST(SniCluster, ConfigTest) {
  NiceMock<Server::Configuration::MockFactoryContext> context;
  SniClusterNetworkFilterConfigFactory factory;

  Network::FilterFactoryCb cb =
      factory.createFilterFactoryFromProto(*factory.createEmptyConfigProto(), context);
  Network::MockConnection connection;
  EXPECT_CALL(connection, addReadFilter(_));
  cb(connection);
}

// Test that per connection filter config is set if SNI is available
TEST(SniCluster, SetTcpProxyClusterOnlyIfSniIsPresent) {
  NiceMock<Network::MockReadFilterCallbacks> filter_callbacks;

  RequestInfo::FilterStateImpl per_connection_state;
  ON_CALL(filter_callbacks.connection_, perConnectionState())
      .WillByDefault(ReturnRef(per_connection_state));
  ON_CALL(Const(filter_callbacks.connection_), perConnectionState())
      .WillByDefault(ReturnRef(per_connection_state));

  SniClusterFilter filter;
  filter.initializeReadFilterCallbacks(filter_callbacks);

  // no sni
  {
    ON_CALL(filter_callbacks.connection_, requestedServerName())
        .WillByDefault(Return(EMPTY_STRING));
    filter.onNewConnection();

    EXPECT_FALSE(per_connection_state.hasData<TcpProxy::PerConnectionCluster>(
        TcpProxy::PerConnectionCluster::Key));
  }

  // with sni
  {
    ON_CALL(filter_callbacks.connection_, requestedServerName())
        .WillByDefault(Return("filter_state_cluster"));
    filter.onNewConnection();

    EXPECT_TRUE(per_connection_state.hasData<TcpProxy::PerConnectionCluster>(
        TcpProxy::PerConnectionCluster::Key));

    auto per_connection_cluster = per_connection_state.getData<TcpProxy::PerConnectionCluster>(
        TcpProxy::PerConnectionCluster::Key);
    EXPECT_EQ(per_connection_cluster.value(), "filter_state_cluster");
  }
}

} // namespace SniCluster
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
