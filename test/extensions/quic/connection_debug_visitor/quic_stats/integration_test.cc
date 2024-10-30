#include "envoy/extensions/quic/connection_debug_visitor/quic_stats/v3/quic_stats.pb.h"

#include "test/integration/http_integration.h"

namespace Envoy {
namespace Extensions {
namespace Quic {
namespace ConnectionDebugVisitors {
namespace QuicStats {

class QuicStatsIntegrationTest : public testing::TestWithParam<Network::Address::IpVersion>,
                                 public HttpIntegrationTest {
public:
  QuicStatsIntegrationTest() : HttpIntegrationTest(Http::CodecType::HTTP3, GetParam()) {
    config_helper_.addConfigModifier(
        [=](envoy::config::bootstrap::v3::Bootstrap& bootstrap) -> void {
          auto listener = bootstrap.mutable_static_resources()->mutable_listeners(0);
          auto debug_visitor_config = listener->mutable_udp_listener_config()
                                          ->mutable_quic_options()
                                          ->mutable_connection_debug_visitor_config();

          envoy::extensions::quic::connection_debug_visitor::quic_stats::v3::Config config;
          debug_visitor_config->mutable_typed_config()->PackFrom(config);
          debug_visitor_config->set_name("envoy.quic.connection_debug_visitor.quic_stats");

          listener->set_stat_prefix("test");
        });
  }
};
INSTANTIATE_TEST_SUITE_P(IpVersions, QuicStatsIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

// Verify that:
// * Stats are in the correct scope/namespace.
// * The data from QUICHE is plausibly correct.
TEST_P(QuicStatsIntegrationTest, Basic) {
  auto begin = std::chrono::steady_clock::now(); // NO_CHECK_FORMAT(real_time)
  testRouterHeaderOnlyRequestAndResponse();
  codec_client_->goAway();
  codec_client_->close(Network::ConnectionCloseType::FlushWrite);
  auto end = std::chrono::steady_clock::now(); // NO_CHECK_FORMAT(real_time)

  // Record the duration of the test to use as an upper bound on the round trip time measurement.
  const std::chrono::microseconds test_duration_us =
      std::chrono::duration_cast<std::chrono::microseconds>(end - begin);

  // Validate that these stats exist (in the correct namespace), and wait for values to be available
  // before validating values and ranges. Gauges/counters and histograms go through slightly
  // different paths, so check each to avoid test flakes.
  test_server_->waitUntilHistogramHasSamples("listener.test.quic_stats.cx_rtt_us");
  test_server_->waitForCounterGe("listener.test.quic_stats.cx_tx_packets_total", 1);

  auto validateCounterRange = [this](const std::string& name, uint64_t lower, uint64_t upper) {
    auto counter = test_server_->counter(absl::StrCat("listener.test.quic_stats.", name));
    ENVOY_LOG(info, "counter {}: {}, expected range {}-{}", name, counter->value(), lower, upper);
    EXPECT_GE(counter->value(), lower);
    EXPECT_LE(counter->value(), upper);
  };
  auto validateHistogramRange = [this](const std::string& name, int64_t lower, int64_t upper) {
    auto histogram = test_server_->histogram(absl::StrCat("listener.test.quic_stats.", name));
    auto& summary = histogram->cumulativeStatistics();
    ENVOY_LOG(info, "histogram {}: {}, expected range {}-{}", name, summary.sampleSum(), lower,
              upper);

    // With only 1 sample, the `sampleSum()` is the one value that has been recorded.
    EXPECT_EQ(1, summary.sampleCount());
    EXPECT_GE(summary.sampleSum(), lower);
    EXPECT_LE(summary.sampleSum(), upper);
  };

  // These values are intentionally very loose to avoid test flakes. They're just trying to verify
  // that the values are at least in the approximate range of what we expect, and in the units we
  // expect.
  validateCounterRange("cx_tx_packets_retransmitted_total", 0, 10);
  validateCounterRange("cx_tx_packets_total", 2, 20);
  validateCounterRange("cx_tx_amplification_throttling_total", 0, 1);
  validateCounterRange("cx_rx_packets_total", 2, 20);
  validateCounterRange("cx_path_degrading_total", 0, 2);
  validateCounterRange("cx_forward_progress_after_path_degrading_total", 0, 2);
  validateHistogramRange("cx_rtt_us", 1, test_duration_us.count());
  validateHistogramRange("cx_tx_estimated_bandwidth", 100,
                         1024ULL * 1024ULL * 1024ULL * 1024ULL /* 1 TB/s */);
  validateHistogramRange("cx_tx_percent_retransmitted_packets", 0, 10);
  validateHistogramRange("cx_tx_mtu", 500, 65535);
  validateHistogramRange("cx_rx_mtu", 500, 65535);
}

} // namespace QuicStats
} // namespace ConnectionDebugVisitors
} // namespace Quic
} // namespace Extensions
} // namespace Envoy
