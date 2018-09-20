#include "test/mocks/network/mocks.h"
#include "test/mocks/server/mocks.h"

#include "extensions/filters/network/sni_cluster/sni_cluster.h"

#include "common/tcp_proxy/tcp_proxy.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace SniCluster {

// Test that a SniCluster filter config works.
TEST(SniCluster, ConfigTest) {
  NiceMock<Server::Configuration::MockFactoryContext> context;
  SniClusterNetworkFilterConfigFactory factory;

  Network::FilterFactoryCb cb = factory.createFilterFactoryFromProto(*factory.createEmptyConfigProto(), context);
  Network::MockConnection connection;
  EXPECT_CALL(connection, addReadFilter(_));
  cb(connection);
}

// Test that per connection filter config is set if SNI is available
TEST(SniCluster, SetTcpProxyClusterOnlyIfSniIsPresent) {
  NiceMock<Network::MockReadFilterCallbacks> filter_callbacks;
  SniClusterFilter filter;
  filter.initializeReadCallbacks(filter_callbacks);

  // no sni
  {
    ON_CALL(filter_callbacks.connection, requestedServerName).WillByDefault(Return(EMPTY_STRING));
    filter.onNewConnection();

    EXPECT_EQ(filter_callbacks.connection.per_connection_state_.hasData(Envoy::TcpProxy::PerConnectionTcpProxyConfig::CLUSTER_KEY), false);
  }

  // with sni
  {
    ON_CALL(filter_callbacks.connection, requestedServerName).WillByDefault(Return("filter_state_cluster"));
    filter.onNewConnection();

    EXPECT_EQ(filter_callbacks.connection.per_connection_state_.hasData(Envoy::TcpProxy::PerConnectionTcpProxyConfig::CLUSTER_KEY), true);

    auto per_connection_config = filter_callbacks.connection.per_connection_state_.getData<Envoy::TcpProxy::PerConnectionTcpProxyConfig>(Envoy::TcpProxy::PerConnectionTcpProxyConfig::CLUSTER_KEY);
    EXPECT_EQ(per_connection_config.cluster(), "filter_state_cluster");
  }
}

} // namespace SniCluster
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
