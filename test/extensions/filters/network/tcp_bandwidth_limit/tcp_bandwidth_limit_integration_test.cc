#include "test/integration/integration.h"

namespace Envoy {
namespace {

class TcpBandwidthLimitIntegrationTest : public Event::TestUsingSimulatedTime,
                                         public testing::TestWithParam<Network::Address::IpVersion>,
                                         public BaseIntegrationTest {
public:
  TcpBandwidthLimitIntegrationTest()
      : BaseIntegrationTest(GetParam(), ConfigHelper::tcpProxyConfig()) {}

  void setup(const std::string& filter_yaml) {
    config_helper_.addNetworkFilter(filter_yaml);
    BaseIntegrationTest::initialize();
  }
};

INSTANTIATE_TEST_SUITE_P(IpVersions, TcpBandwidthLimitIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(TcpBandwidthLimitIntegrationTest, DownloadLimiting) {
  setup(R"EOF(
name: envoy.filters.network.tcp_bandwidth_limit
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.network.tcp_bandwidth_limit.v3.TcpBandwidthLimit
  stat_prefix: tcp_bw
  download_limit_kbps: 1
)EOF");

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("listener_0"));
  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));

  ASSERT_TRUE(tcp_client->write("hello"));
  ASSERT_TRUE(fake_upstream_connection->waitForData(5));

  ASSERT_TRUE(fake_upstream_connection->write("world"));
  tcp_client->waitForData("world");

  EXPECT_EQ(test_server_->counter("tcp_bw.tcp_bandwidth_limit.download_enabled")->value(), 1);
  EXPECT_EQ(test_server_->counter("tcp_bw.tcp_bandwidth_limit.download_throttled")->value(), 0);

  tcp_client->close();
  ASSERT_TRUE(fake_upstream_connection->waitForDisconnect());
}

TEST_P(TcpBandwidthLimitIntegrationTest, UploadLimiting) {
  setup(R"EOF(
name: envoy.filters.network.tcp_bandwidth_limit
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.network.tcp_bandwidth_limit.v3.TcpBandwidthLimit
  stat_prefix: tcp_bw
  upload_limit_kbps: 1
)EOF");

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("listener_0"));
  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));

  ASSERT_TRUE(fake_upstream_connection->write("hello"));
  tcp_client->waitForData("hello");

  EXPECT_EQ(test_server_->counter("tcp_bw.tcp_bandwidth_limit.upload_enabled")->value(), 1);
  EXPECT_EQ(test_server_->counter("tcp_bw.tcp_bandwidth_limit.upload_throttled")->value(), 0);

  tcp_client->close();
  ASSERT_TRUE(fake_upstream_connection->waitForDisconnect());
}

TEST_P(TcpBandwidthLimitIntegrationTest, DownloadThrottled) {
  setup(R"EOF(
name: envoy.filters.network.tcp_bandwidth_limit
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.network.tcp_bandwidth_limit.v3.TcpBandwidthLimit
  stat_prefix: tcp_bw
  download_limit_kbps: 1
)EOF");

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("listener_0"));
  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));

  const std::string large_data(2048, 'a');
  ASSERT_TRUE(tcp_client->write(large_data));

  test_server_->waitForCounterGe("tcp_bw.tcp_bandwidth_limit.download_throttled", 1);
  test_server_->waitForGaugeEq("tcp_bw.tcp_bandwidth_limit.download_bytes_buffered", 1024);

  tcp_client->close();
  ASSERT_TRUE(fake_upstream_connection->waitForDisconnect());
}

TEST_P(TcpBandwidthLimitIntegrationTest, UploadThrottled) {
  setup(R"EOF(
name: envoy.filters.network.tcp_bandwidth_limit
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.network.tcp_bandwidth_limit.v3.TcpBandwidthLimit
  stat_prefix: tcp_bw
  upload_limit_kbps: 1
)EOF");

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("listener_0"));
  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));

  const std::string large_data(2048, 'b');
  ASSERT_TRUE(fake_upstream_connection->write(large_data));

  test_server_->waitForCounterGe("tcp_bw.tcp_bandwidth_limit.upload_throttled", 1);
  test_server_->waitForGaugeEq("tcp_bw.tcp_bandwidth_limit.upload_bytes_buffered", 1024);

  tcp_client->close();
  ASSERT_TRUE(fake_upstream_connection->waitForDisconnect());
}

} // namespace
} // namespace Envoy
