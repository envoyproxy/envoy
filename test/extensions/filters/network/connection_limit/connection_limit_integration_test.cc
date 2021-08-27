#include "test/integration/integration.h"

namespace Envoy {
namespace {

class ConnectionLimitIntegrationTest : public Event::TestUsingSimulatedTime,
                                       public testing::TestWithParam<Network::Address::IpVersion>,
                                       public BaseIntegrationTest {
public:
  ConnectionLimitIntegrationTest()
      : BaseIntegrationTest(GetParam(), ConfigHelper::tcpProxyConfig()) {}

  void setup(const std::string& filter_yaml) {
    config_helper_.addNetworkFilter(filter_yaml);
    BaseIntegrationTest::initialize();
  }
};

INSTANTIATE_TEST_SUITE_P(IpVersions, ConnectionLimitIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

// Make sure the filter works in the basic case.
TEST_P(ConnectionLimitIntegrationTest, NoConnectionLimiting) {
  setup(R"EOF(
name: connectionlimit
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.network.connection_limit.v3.ConnectionLimit
  stat_prefix: connection_limit_stats
  max_connections: 1
  delay: 0.2s
)EOF");

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("listener_0"));
  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));
  ASSERT_TRUE(tcp_client->write("hello"));
  ASSERT_TRUE(fake_upstream_connection->waitForData(5));
  ASSERT_TRUE(fake_upstream_connection->write("world"));
  tcp_client->waitForData("world");

  EXPECT_EQ(
      1,
      test_server_->gauge("connection_limit.connection_limit_stats.active_connections")->value());

  tcp_client->close();
  ASSERT_TRUE(fake_upstream_connection->waitForDisconnect());

  test_server_->waitForGaugeEq("connection_limit.connection_limit_stats.active_connections", 0,
                               std::chrono::milliseconds(100));

  EXPECT_EQ(0, test_server_->counter("connection_limit.connection_limit_stats.limited_connections")
                   ->value());
}

// Make sure the filter works in the connection limit case.
TEST_P(ConnectionLimitIntegrationTest, ConnectionLimiting) {
  setup(R"EOF(
name: connectionlimit
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.network.connection_limit.v3.ConnectionLimit
  stat_prefix: connection_limit_stats
  max_connections: 1
  delay: 0.2s
)EOF");

  IntegrationTcpClientPtr tcp_client1 = makeTcpConnection(lookupPort("listener_0"));
  IntegrationTcpClientPtr tcp_client2 = makeTcpConnection(lookupPort("listener_0"));

  FakeRawConnectionPtr fake_upstream_connection1;
  FakeRawConnectionPtr fake_upstream_connection2;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection1) ||
              fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection2));

  test_server_->waitForGaugeEq("connection_limit.connection_limit_stats.active_connections", 1,
                               std::chrono::milliseconds(200));

  tcp_client1->close();
  tcp_client2->close();

  test_server_->waitForGaugeEq("connection_limit.connection_limit_stats.active_connections", 0,
                               std::chrono::milliseconds(100));

  EXPECT_EQ(1, test_server_->counter("connection_limit.connection_limit_stats.limited_connections")
                   ->value());
}

} // namespace
} // namespace Envoy
