#include "source/common/stream_info/stream_info_impl.h"

#include "test/test_common/simulated_time_system.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "library/common/stream_info/extra_stream_info.h"

namespace Envoy {
namespace StreamInfo {

static constexpr uint64_t SYSTEM_TIME_START_MS = 10000;
static constexpr uint64_t MONOTONIC_TIME_START_MS = 1000;

class StalledTimeSource : public TimeSource {
public:
  StalledTimeSource(uint64_t system_time_ms, uint64_t monotonic_time_ms) {
    stalled_system_time_ = SystemTime(std::chrono::milliseconds(system_time_ms));
    stalled_monotonic_time_ = MonotonicTime(std::chrono::milliseconds(monotonic_time_ms));
  }
  SystemTime systemTime() override { return stalled_system_time_; }
  MonotonicTime monotonicTime() override { return stalled_monotonic_time_; }

private:
  SystemTime stalled_system_time_;
  MonotonicTime stalled_monotonic_time_;
};

class FinalIntelTest : public testing::Test {
public:
  void checkEquality(envoy_final_stream_intel& a, envoy_final_stream_intel& b) {
    EXPECT_EQ(a.sending_start_ms, b.sending_start_ms);
    EXPECT_EQ(a.sending_end_ms, b.sending_end_ms);
    EXPECT_EQ(a.connect_start_ms, b.connect_start_ms);
    EXPECT_EQ(a.connect_end_ms, b.connect_end_ms);
    EXPECT_EQ(a.ssl_start_ms, b.ssl_start_ms);
    EXPECT_EQ(a.ssl_end_ms, b.ssl_end_ms);
    EXPECT_EQ(a.socket_reused, b.socket_reused);
    EXPECT_EQ(a.stream_start_ms, b.stream_start_ms);
    EXPECT_EQ(a.stream_end_ms, b.stream_end_ms);
    EXPECT_EQ(a.dns_start_ms, b.dns_start_ms);
    EXPECT_EQ(a.dns_end_ms, b.dns_end_ms);
    EXPECT_EQ(a.sent_byte_count, b.sent_byte_count);
    EXPECT_EQ(a.received_byte_count, b.received_byte_count);
  }
  StalledTimeSource start_time_source_{SYSTEM_TIME_START_MS, MONOTONIC_TIME_START_MS};
  envoy_final_stream_intel final_intel_{-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 0, 0, 0, 0, -1};
  envoy_final_stream_intel expected_intel_{-1, -1, -1, -1, -1, -1, -1, -1,
                                           -1, -1, -1, 0,  0,  0,  0,  -1};
};

TEST_F(FinalIntelTest, Unset) {
  StreamInfoImpl stream_info{Http::Protocol::Http2, start_time_source_, nullptr};
  // By convention the "StreamInfoImpl instantiation" is when the request starts.
  expected_intel_.stream_start_ms = SYSTEM_TIME_START_MS;
  // The SystemTime (11111) is irrelevant for the fake_current_time.
  StalledTimeSource fake_current_time(11111, MONOTONIC_TIME_START_MS + 500);
  expected_intel_.stream_end_ms = SYSTEM_TIME_START_MS + 500;

  setFinalStreamIntel(stream_info, fake_current_time, final_intel_);

  checkEquality(final_intel_, expected_intel_);
}

TEST_F(FinalIntelTest, SetWithSsl) {
  StreamInfoImpl stream_info{Http::Protocol::Http2, start_time_source_, nullptr};
  stream_info.setUpstreamInfo(std::make_shared<UpstreamInfoImpl>());
  auto upstream_info = stream_info.upstreamInfo();
  auto& timing = upstream_info->upstreamTiming();

  upstream_info->setUpstreamProtocol(Http::Protocol::Http2);
  expected_intel_.upstream_protocol = 2;

  expected_intel_.stream_start_ms = SYSTEM_TIME_START_MS;
  timing.first_upstream_tx_byte_sent_ =
      MonotonicTime(std::chrono::milliseconds(MONOTONIC_TIME_START_MS + 100));
  expected_intel_.sending_start_ms = SYSTEM_TIME_START_MS + 100;
  timing.last_upstream_tx_byte_sent_ =
      MonotonicTime(std::chrono::milliseconds(MONOTONIC_TIME_START_MS + 200));
  expected_intel_.sending_end_ms = SYSTEM_TIME_START_MS + 200;
  timing.first_upstream_rx_byte_received_ =
      MonotonicTime(std::chrono::milliseconds(MONOTONIC_TIME_START_MS + 300));
  expected_intel_.response_start_ms = SYSTEM_TIME_START_MS + 300;
  timing.upstream_connect_start_ =
      MonotonicTime(std::chrono::milliseconds(MONOTONIC_TIME_START_MS + 400));
  expected_intel_.connect_start_ms = SYSTEM_TIME_START_MS + 400;
  timing.upstream_connect_complete_ =
      MonotonicTime(std::chrono::milliseconds(MONOTONIC_TIME_START_MS + 500));
  expected_intel_.connect_end_ms = SYSTEM_TIME_START_MS + 500;
  expected_intel_.ssl_start_ms = SYSTEM_TIME_START_MS + 500;
  timing.upstream_handshake_complete_ =
      MonotonicTime(std::chrono::milliseconds(MONOTONIC_TIME_START_MS + 600));
  expected_intel_.ssl_end_ms = SYSTEM_TIME_START_MS + 600;
  StalledTimeSource fake_current_time(11111, MONOTONIC_TIME_START_MS + 700);
  expected_intel_.stream_end_ms = SYSTEM_TIME_START_MS + 700;

  upstream_info->setUpstreamNumStreams(5);
  expected_intel_.socket_reused = 1;

  setFinalStreamIntel(stream_info, fake_current_time, final_intel_);

  checkEquality(final_intel_, expected_intel_);
}

TEST_F(FinalIntelTest, SetWithoutSsl) {
  StreamInfoImpl stream_info{Http::Protocol::Http11, start_time_source_, nullptr};
  stream_info.setUpstreamInfo(std::make_shared<UpstreamInfoImpl>());
  auto upstream_info = stream_info.upstreamInfo();
  auto& timing = upstream_info->upstreamTiming();

  expected_intel_.stream_start_ms = SYSTEM_TIME_START_MS;
  timing.first_upstream_tx_byte_sent_ =
      MonotonicTime(std::chrono::milliseconds(MONOTONIC_TIME_START_MS + 100));
  expected_intel_.sending_start_ms = SYSTEM_TIME_START_MS + 100;
  timing.last_upstream_tx_byte_sent_ =
      MonotonicTime(std::chrono::milliseconds(MONOTONIC_TIME_START_MS + 200));
  expected_intel_.sending_end_ms = SYSTEM_TIME_START_MS + 200;
  timing.first_upstream_rx_byte_received_ =
      MonotonicTime(std::chrono::milliseconds(MONOTONIC_TIME_START_MS + 300));
  expected_intel_.response_start_ms = SYSTEM_TIME_START_MS + 300;
  timing.upstream_connect_start_ =
      MonotonicTime(std::chrono::milliseconds(MONOTONIC_TIME_START_MS + 400));
  expected_intel_.connect_start_ms = SYSTEM_TIME_START_MS + 400;
  timing.upstream_connect_complete_ =
      MonotonicTime(std::chrono::milliseconds(MONOTONIC_TIME_START_MS + 500));
  expected_intel_.connect_end_ms = SYSTEM_TIME_START_MS + 500;
  expected_intel_.ssl_start_ms = -1;
  expected_intel_.ssl_end_ms = -1;
  StalledTimeSource fake_current_time(11111, MONOTONIC_TIME_START_MS + 600);
  expected_intel_.stream_end_ms = SYSTEM_TIME_START_MS + 600;

  upstream_info->setUpstreamNumStreams(5);
  expected_intel_.socket_reused = 1;

  setFinalStreamIntel(stream_info, fake_current_time, final_intel_);

  checkEquality(final_intel_, expected_intel_);
}

} // namespace StreamInfo
} // namespace Envoy
