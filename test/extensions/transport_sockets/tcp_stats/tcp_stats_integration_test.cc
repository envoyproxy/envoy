#if defined(__linux__)
#include "envoy/extensions/transport_sockets/raw_buffer/v3/raw_buffer.pb.h"
#include "envoy/extensions/transport_sockets/raw_buffer/v3/raw_buffer.pb.validate.h"
#include "envoy/extensions/transport_sockets/tcp_stats/v3/tcp_stats.pb.h"

#include "test/integration/integration.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace TcpStats {
namespace {
class TcpStatsSocketIntegrationTest : public testing::TestWithParam<Network::Address::IpVersion>,
                                      public BaseIntegrationTest {
public:
  TcpStatsSocketIntegrationTest()
      : BaseIntegrationTest(GetParam(), ConfigHelper::tcpProxyConfig()) {}

  void initialize() override {
    config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      envoy::config::core::v3::TransportSocket inner_socket;
      inner_socket.set_name("envoy.transport_sockets.raw_buffer");
      envoy::extensions::transport_sockets::raw_buffer::v3::RawBuffer raw_buffer;
      inner_socket.mutable_typed_config()->PackFrom(raw_buffer);
      envoy::extensions::transport_sockets::tcp_stats::v3::Config proto_config;
      proto_config.mutable_transport_socket()->MergeFrom(inner_socket);

      auto* cluster_transport_socket =
          bootstrap.mutable_static_resources()->mutable_clusters(0)->mutable_transport_socket();
      cluster_transport_socket->set_name("envoy.transport_sockets.tcp_stats");
      cluster_transport_socket->mutable_typed_config()->PackFrom(proto_config);

      auto* listener = bootstrap.mutable_static_resources()->mutable_listeners(0);
      auto* listener_transport_socket =
          listener->mutable_filter_chains(0)->mutable_transport_socket();
      listener_transport_socket->set_name("envoy.transport_sockets.tcp_stats");
      listener_transport_socket->mutable_typed_config()->PackFrom(proto_config);

      listener->set_stat_prefix("test");
    });
    BaseIntegrationTest::initialize();
  }

  FakeRawConnectionPtr fake_upstream_connection_;
};

INSTANTIATE_TEST_SUITE_P(IpVersions, TcpStatsSocketIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

// Verify that:
// * Stats are in the correct scope/namespace.
// * The syscall to get the data is producing meaningful results.
TEST_P(TcpStatsSocketIntegrationTest, Basic) {
  initialize();

  auto begin = std::chrono::steady_clock::now(); // NO_CHECK_FORMAT(real_time)

  auto listener_port = lookupPort("listener_0");
  auto tcp_client = makeTcpConnection(listener_port);
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection_));

  ASSERT_TRUE(tcp_client->write("data"));
  ASSERT_TRUE(fake_upstream_connection_->waitForData(4));
  ASSERT_TRUE(fake_upstream_connection_->write("response"));
  tcp_client->waitForData("response");

  tcp_client->close();
  ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());

  auto end = std::chrono::steady_clock::now(); // NO_CHECK_FORMAT(real_time)

  // Record the duration of the test to use as an upper bound on the round trip time measurement.
  std::chrono::microseconds test_duration_us =
      std::chrono::duration_cast<std::chrono::microseconds>(end - begin);

  // Validate that these stats exist (in the correct namespace), and Wait for values to be available
  // before validating values and ranges. Gauges/counters and histograms go through slightly
  // different paths, so check each to avoid test flakes.
  test_server_->waitUntilHistogramHasSamples("cluster.cluster_0.tcp_stats.cx_rtt_us");
  test_server_->waitForCounterGe("cluster.cluster_0.tcp_stats.cx_tx_segments", 1);
  test_server_->waitUntilHistogramHasSamples("listener.test.tcp_stats.cx_rtt_us");
  test_server_->waitForCounterGe("listener.test.tcp_stats.cx_tx_segments", 1);

  auto validateCounterRange = [this](const std::string& name, uint64_t lower, uint64_t upper) {
    auto counter = test_server_->counter(absl::StrCat("cluster.cluster_0.tcp_stats.", name));
    EXPECT_GE(counter->value(), lower);
    EXPECT_LE(counter->value(), upper);
  };
  auto validateGaugeRange = [this](const std::string& name, int64_t lower, int64_t upper) {
    auto counter = test_server_->gauge(absl::StrCat("cluster.cluster_0.tcp_stats.", name));
    EXPECT_GE(counter->value(), lower);
    EXPECT_LE(counter->value(), upper);
  };
  auto validateHistogramRange = [this](const std::string& name, int64_t lower, int64_t upper) {
    auto histogram = test_server_->histogram(absl::StrCat("cluster.cluster_0.tcp_stats.", name));
    auto& summary = histogram->cumulativeStatistics();

    // With only 1 sample, the `sampleSum()` is the one value that has been recorded.
    EXPECT_EQ(1, summary.sampleCount());
    EXPECT_GE(summary.sampleSum(), lower);
    EXPECT_LE(summary.sampleSum(), upper);
  };

  // These values are intentionally very loose to avoid test flakes. They're just trying to verify
  // that the values are at least in the approximate range of what we expect, and in the units we
  // expect.
  validateCounterRange("cx_tx_segments", 2, 20);
  validateCounterRange("cx_rx_segments", 2, 20);
  validateCounterRange("cx_tx_data_segments", 1, 10);
  validateCounterRange("cx_rx_data_segments", 1, 10);
  validateCounterRange("cx_tx_retransmitted_segments", 0, 10);

  // After the connection is closed, there should be no unsent or unacked data.
  validateGaugeRange("cx_tx_unsent_bytes", 0, 0);
  validateGaugeRange("cx_tx_unacked_segments", 0, 0);

  validateHistogramRange("cx_tx_percent_retransmitted_segments", 0,
                         Stats::Histogram::PercentScale); // 0-100%
  validateHistogramRange("cx_rtt_us", 1, test_duration_us.count());
  validateHistogramRange("cx_rtt_variance_us", 1, test_duration_us.count());
}

} // namespace
} // namespace TcpStats
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
#endif
