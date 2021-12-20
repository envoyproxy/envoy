#include "source/common/stream_info/stream_info_impl.h"

#include "test/test_common/simulated_time_system.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "library/common/stream_info/extra_stream_info.h"

namespace Envoy {
namespace StreamInfo {

class FinalIntelTest : public testing::Test {
public:
  FinalIntelTest() {
    memset(&final_intel_, 0, sizeof(envoy_final_stream_intel));
    memset(&expected_intel_, 0, sizeof(envoy_final_stream_intel));
  }

  void checkEquality(envoy_final_stream_intel& a, envoy_final_stream_intel& b) {
    EXPECT_EQ(a.sending_start_ms, b.sending_start_ms);
    EXPECT_EQ(a.sending_end_ms, b.sending_end_ms);
    EXPECT_EQ(a.connect_start_ms, b.connect_start_ms);
    EXPECT_EQ(a.connect_end_ms, b.connect_end_ms);
    EXPECT_EQ(a.ssl_start_ms, b.ssl_start_ms);
    EXPECT_EQ(a.ssl_end_ms, b.ssl_end_ms);
    EXPECT_EQ(a.socket_reused, b.socket_reused);
    EXPECT_EQ(a.request_start_ms, b.request_start_ms);
    EXPECT_EQ(a.request_end_ms, b.request_end_ms);
    EXPECT_EQ(a.dns_start_ms, b.dns_start_ms);
    EXPECT_EQ(a.dns_end_ms, b.dns_end_ms);
    EXPECT_EQ(a.sent_byte_count, b.sent_byte_count);
    EXPECT_EQ(a.received_byte_count, b.received_byte_count);
  }

  Event::SimulatedTimeSystem test_time_;
  StreamInfoImpl stream_info_{test_time_.timeSystem(), nullptr};
  envoy_final_stream_intel final_intel_;
  envoy_final_stream_intel expected_intel_;
};

TEST_F(FinalIntelTest, Unset) {
  setFinalStreamIntel(stream_info_, final_intel_);
  checkEquality(final_intel_, expected_intel_);
}

TEST_F(FinalIntelTest, Set) {
  stream_info_.setUpstreamInfo(std::make_shared<UpstreamInfoImpl>());
  auto upstream_info = stream_info_.upstreamInfo();
  auto& timing = upstream_info->upstreamTiming();

  timing.first_upstream_tx_byte_sent_ = MonotonicTime(std::chrono::milliseconds(100));
  expected_intel_.sending_start_ms = 100;
  timing.last_upstream_tx_byte_sent_ = MonotonicTime(std::chrono::milliseconds(200));
  expected_intel_.sending_end_ms = 200;
  timing.first_upstream_rx_byte_received_ = MonotonicTime(std::chrono::milliseconds(300));
  expected_intel_.response_start_ms = 300;
  timing.upstream_connect_start_ = MonotonicTime(std::chrono::milliseconds(400));
  expected_intel_.connect_start_ms = 400;
  timing.upstream_connect_complete_ = MonotonicTime(std::chrono::milliseconds(500));
  expected_intel_.connect_end_ms = 500;
  expected_intel_.ssl_start_ms = 500;
  timing.upstream_handshake_complete_ = MonotonicTime(std::chrono::milliseconds(600));
  expected_intel_.ssl_end_ms = 600;

  upstream_info->setUpstreamNumStreams(5);
  expected_intel_.socket_reused = 1;
  setFinalStreamIntel(stream_info_, final_intel_);
  checkEquality(final_intel_, expected_intel_);
}

} // namespace StreamInfo
} // namespace Envoy
