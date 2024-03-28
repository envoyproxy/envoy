#include "source/common/tcp_proxy/tcp_proxy.h"
#include "source/extensions/filters/network/sni_cluster/config.h"
#include "source/extensions/filters/network/sni_cluster/sni_cluster.h"

#include "test/mocks/network/mocks.h"
#include "test/mocks/server/factory_context.h"
#include "test/mocks/stream_info/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
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
      factory.createFilterFactoryFromProto(*factory.createEmptyConfigProto(), context).value();
  Network::MockConnection connection;
  EXPECT_CALL(connection, addReadFilter(_));
  cb(connection);
}

// Test that per connection filter config is set if SNI is available
TEST(SniCluster, SetTcpProxyClusterOnlyIfSniIsPresent) {
  NiceMock<Network::MockReadFilterCallbacks> filter_callbacks;

  NiceMock<StreamInfo::MockStreamInfo> stream_info;
  ON_CALL(filter_callbacks.connection_, streamInfo()).WillByDefault(ReturnRef(stream_info));
  ON_CALL(Const(filter_callbacks.connection_), streamInfo()).WillByDefault(ReturnRef(stream_info));

  SniClusterFilter filter;
  filter.initializeReadFilterCallbacks(filter_callbacks);

  // no sni
  {
    ON_CALL(filter_callbacks.connection_, requestedServerName())
        .WillByDefault(Return(EMPTY_STRING));
    filter.onNewConnection();

    EXPECT_FALSE(stream_info.filterState()->hasData<TcpProxy::PerConnectionCluster>(
        TcpProxy::PerConnectionCluster::key()));
  }

  // with sni
  {
    ON_CALL(filter_callbacks.connection_, requestedServerName())
        .WillByDefault(Return("filter_state_cluster"));
    filter.onNewConnection();

    EXPECT_TRUE(stream_info.filterState()->hasData<TcpProxy::PerConnectionCluster>(
        TcpProxy::PerConnectionCluster::key()));

    auto per_connection_cluster =
        stream_info.filterState()->getDataReadOnly<TcpProxy::PerConnectionCluster>(
            TcpProxy::PerConnectionCluster::key());
    EXPECT_EQ(per_connection_cluster->value(), "filter_state_cluster");
  }
}

} // namespace SniCluster
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
