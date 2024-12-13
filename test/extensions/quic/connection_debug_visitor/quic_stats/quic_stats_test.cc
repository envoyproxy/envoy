#include "source/extensions/quic/connection_debug_visitor/quic_stats/quic_stats.h"

#include "test/common/stats/stat_test_utility.h"
#include "test/mocks/event/mocks.h"

#include "gtest/gtest.h"

using testing::AtLeast;
using testing::NiceMock;

namespace Envoy {
namespace Extensions {
namespace Quic {
namespace ConnectionDebugVisitors {
namespace QuicStats {

// Variant of QuicStatsVisitor for testing that has it's own stats, instead of requiring mocks
// for several quiche objects.
class TestQuicStatsVisitor : public QuicStatsVisitor {
public:
  using QuicStatsVisitor::QuicStatsVisitor;

  const quic::QuicConnectionStats& getQuicStats() override { return stats_; }

  quic::QuicConnectionStats stats_;
};

class QuicStatsTest : public testing::Test {
public:
  void initialize(bool enable_periodic) {
    envoy::extensions::quic::connection_debug_visitor::quic_stats::v3::Config cfg;
    if (enable_periodic) {
      cfg.mutable_update_period()->MergeFrom(ProtobufUtil::TimeUtil::MillisecondsToDuration(1000));
      timer_ = new NiceMock<Event::MockTimer>(&dispatcher_);
      EXPECT_CALL(*timer_, enableTimer(std::chrono::milliseconds(1000), _)).Times(AtLeast(1));
    }
    config_ = std::make_unique<Config>(cfg, *store_.rootScope());
    quic_stats_ = std::make_unique<TestQuicStatsVisitor>(*config_, dispatcher_);
  }

  uint64_t counterValue(absl::string_view name) {
    auto opt_ref = store_.findCounterByString(absl::StrCat("quic_stats.", name));
    ASSERT(opt_ref.has_value());
    return opt_ref.value().get().value();
  }

  absl::optional<uint64_t> histogramValue(absl::string_view name) {
    std::vector<uint64_t> values = store_.histogramValues(absl::StrCat("quic_stats.", name), true);
    ASSERT(values.size() <= 1,
           absl::StrCat(name, " didn't have <=1 value, instead had ", values.size()));
    if (values.empty()) {
      return absl::nullopt;
    } else {
      return values[0];
    }
  }

  Stats::TestUtil::TestStore store_;
  NiceMock<Event::MockTimer>* timer_;
  NiceMock<Event::MockDispatcher> dispatcher_;
  std::unique_ptr<Config> config_;
  std::unique_ptr<TestQuicStatsVisitor> quic_stats_;
};

// Validate that the configured update_period is honored, and that stats are updated when the timer
// fires.
TEST_F(QuicStatsTest, Periodic) {
  initialize(true);

  EXPECT_CALL(*timer_, enableTimer(std::chrono::milliseconds(1000), _));
  quic_stats_->stats_.packets_sent = 42;
  timer_->callback_();
  EXPECT_EQ(42, counterValue("cx_tx_packets_total"));

  EXPECT_CALL(*timer_, disableTimer());
  quic_stats_->OnConnectionClosed(quic::QuicConnectionCloseFrame{},
                                  quic::ConnectionCloseSource::FROM_PEER);
}

// Validate that stats are updated when the connection is closed. Counters should be appropriately
// updated.
TEST_F(QuicStatsTest, CloseSocket) {
  initialize(false);

  quic_stats_->stats_.packets_sent = 42;
  quic_stats_->OnConnectionClosed(quic::QuicConnectionCloseFrame{},
                                  quic::ConnectionCloseSource::FROM_PEER);
  EXPECT_EQ(42, counterValue("cx_tx_packets_total"));
}

// Validate that the emitted values are correct, that delta updates from a counter move the value by
// the delta (not the entire value), and that multiple sockets interact correctly (stats are
// summed).
TEST_F(QuicStatsTest, Values) {
  initialize(true);

  NiceMock<Event::MockTimer>* timer2;
  timer2 = new NiceMock<Event::MockTimer>(&dispatcher_);
  EXPECT_CALL(*timer2, enableTimer(std::chrono::milliseconds(1000), _)).Times(AtLeast(1));
  std::unique_ptr<TestQuicStatsVisitor> quic_stats2 =
      std::make_unique<TestQuicStatsVisitor>(*config_, dispatcher_);

  // After the first call, stats should be set to exactly these values.
  quic_stats_->stats_.packets_retransmitted = 1;
  quic_stats_->stats_.packets_sent = 2;
  quic_stats_->stats_.num_amplification_throttling = 3;
  quic_stats_->stats_.packets_received = 4;
  quic_stats_->stats_.num_path_degrading = 5;
  quic_stats_->stats_.num_forward_progress_after_path_degrading = 6;
  quic_stats_->stats_.srtt_us = 7;
  quic_stats_->stats_.estimated_bandwidth = quic::QuicBandwidth::FromBytesPerSecond(8);
  quic_stats_->stats_.egress_mtu = 9;
  quic_stats_->stats_.ingress_mtu = 10;
  timer_->callback_();
  EXPECT_EQ(1, counterValue("cx_tx_packets_retransmitted_total"));
  EXPECT_EQ(2, counterValue("cx_tx_packets_total"));
  EXPECT_EQ(3, counterValue("cx_tx_amplification_throttling_total"));
  EXPECT_EQ(4, counterValue("cx_rx_packets_total"));
  EXPECT_EQ(5, counterValue("cx_path_degrading_total"));
  EXPECT_EQ(6, counterValue("cx_forward_progress_after_path_degrading_total"));
  EXPECT_EQ(7U, histogramValue("cx_rtt_us"));
  EXPECT_EQ(8U, histogramValue("cx_tx_estimated_bandwidth"));
  EXPECT_EQ((1U * Stats::Histogram::PercentScale) / 2U,
            histogramValue("cx_tx_percent_retransmitted_packets"));
  EXPECT_EQ(9U, histogramValue("cx_tx_mtu"));
  EXPECT_EQ(10U, histogramValue("cx_rx_mtu"));

  // Trigger the timer again with unchanged values. The metrics should be unchanged (but the
  // histograms should have emitted the value again).
  timer_->callback_();
  EXPECT_EQ(1, counterValue("cx_tx_packets_retransmitted_total"));
  EXPECT_EQ(2, counterValue("cx_tx_packets_total"));
  EXPECT_EQ(3, counterValue("cx_tx_amplification_throttling_total"));
  EXPECT_EQ(4, counterValue("cx_rx_packets_total"));
  EXPECT_EQ(5, counterValue("cx_path_degrading_total"));
  EXPECT_EQ(6, counterValue("cx_forward_progress_after_path_degrading_total"));
  EXPECT_EQ(7U, histogramValue("cx_rtt_us"));
  EXPECT_EQ(8U, histogramValue("cx_tx_estimated_bandwidth"));
  // No more packets were transmitted (numerator and denominator deltas are zero), so no value
  // should be emitted.
  EXPECT_EQ(absl::nullopt, histogramValue("cx_tx_percent_retransmitted_packets"));
  EXPECT_EQ(9U, histogramValue("cx_tx_mtu"));
  EXPECT_EQ(10U, histogramValue("cx_rx_mtu"));

  // Set stats on 2nd socket. Values should be combined.
  quic_stats2->stats_.packets_retransmitted = 1;
  quic_stats2->stats_.packets_sent = 1;
  quic_stats2->stats_.num_amplification_throttling = 1;
  quic_stats2->stats_.packets_received = 1;
  quic_stats2->stats_.num_path_degrading = 1;
  quic_stats2->stats_.num_forward_progress_after_path_degrading = 1;
  quic_stats2->stats_.srtt_us = 1;
  quic_stats2->stats_.estimated_bandwidth = quic::QuicBandwidth::FromBytesPerSecond(1);
  quic_stats2->stats_.egress_mtu = 1;
  quic_stats2->stats_.ingress_mtu = 1;
  timer2->callback_();
  EXPECT_EQ(2, counterValue("cx_tx_packets_retransmitted_total"));
  EXPECT_EQ(3, counterValue("cx_tx_packets_total"));
  EXPECT_EQ(4, counterValue("cx_tx_amplification_throttling_total"));
  EXPECT_EQ(5, counterValue("cx_rx_packets_total"));
  EXPECT_EQ(6, counterValue("cx_path_degrading_total"));
  EXPECT_EQ(7, counterValue("cx_forward_progress_after_path_degrading_total"));
  EXPECT_EQ(1U, histogramValue("cx_rtt_us"));
  EXPECT_EQ(1U, histogramValue("cx_tx_estimated_bandwidth"));
  EXPECT_EQ(Stats::Histogram::PercentScale /* 100% */,
            histogramValue("cx_tx_percent_retransmitted_packets"));
  EXPECT_EQ(1U, histogramValue("cx_tx_mtu"));
  EXPECT_EQ(1U, histogramValue("cx_rx_mtu"));

  // Update the first socket again.
  quic_stats_->stats_.packets_retransmitted++;
  quic_stats_->stats_.packets_sent++;
  quic_stats_->stats_.num_amplification_throttling++;
  quic_stats_->stats_.packets_received++;
  quic_stats_->stats_.num_path_degrading++;
  quic_stats_->stats_.num_forward_progress_after_path_degrading++;
  quic_stats_->stats_.srtt_us = 20;
  quic_stats_->stats_.estimated_bandwidth = quic::QuicBandwidth::FromBytesPerSecond(21);
  quic_stats_->stats_.egress_mtu = 22;
  quic_stats_->stats_.ingress_mtu = 23;
  timer_->callback_();
  EXPECT_EQ(3, counterValue("cx_tx_packets_retransmitted_total"));
  EXPECT_EQ(4, counterValue("cx_tx_packets_total"));
  EXPECT_EQ(5, counterValue("cx_tx_amplification_throttling_total"));
  EXPECT_EQ(6, counterValue("cx_rx_packets_total"));
  EXPECT_EQ(7, counterValue("cx_path_degrading_total"));
  EXPECT_EQ(8, counterValue("cx_forward_progress_after_path_degrading_total"));
  EXPECT_EQ(20U, histogramValue("cx_rtt_us"));
  EXPECT_EQ(21U, histogramValue("cx_tx_estimated_bandwidth"));
  EXPECT_EQ(Stats::Histogram::PercentScale /* 100% */,
            histogramValue("cx_tx_percent_retransmitted_packets"));
  EXPECT_EQ(22U, histogramValue("cx_tx_mtu"));
  EXPECT_EQ(23U, histogramValue("cx_rx_mtu"));
}

} // namespace QuicStats
} // namespace ConnectionDebugVisitors
} // namespace Quic
} // namespace Extensions
} // namespace Envoy
