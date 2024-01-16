#include "source/common/common/utility.h"
#include "source/common/http/http2/protocol_constraints.h"

#include "test/common/stats/stat_test_utility.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::HasSubstr;

namespace Envoy {
namespace Http {
namespace Http2 {

class ProtocolConstraintsTest : public ::testing::Test {
protected:
  Http::Http2::CodecStats& http2CodecStats() {
    return Http::Http2::CodecStats::atomicGet(http2_codec_stats_, *stats_store_.rootScope());
  }

  Stats::TestUtil::TestStore stats_store_;
  Http::Http2::CodecStats::AtomicPtr http2_codec_stats_;
  envoy::config::core::v3::Http2ProtocolOptions options_;
};

TEST_F(ProtocolConstraintsTest, DefaultStatusOk) {
  ProtocolConstraints constraints(http2CodecStats(), options_);
  EXPECT_TRUE(constraints.status().ok());
}

TEST_F(ProtocolConstraintsTest, OutboundControlFrameFlood) {
  options_.mutable_max_outbound_frames()->set_value(20);
  options_.mutable_max_outbound_control_frames()->set_value(2);
  ProtocolConstraints constraints(http2CodecStats(), options_);
  constraints.incrementOutboundFrameCount(true);
  constraints.incrementOutboundFrameCount(true);
  EXPECT_TRUE(constraints.checkOutboundFrameLimits().ok());
  constraints.incrementOutboundFrameCount(true);
  EXPECT_FALSE(constraints.checkOutboundFrameLimits().ok());
  EXPECT_TRUE(isBufferFloodError(constraints.status()));
  EXPECT_EQ("Too many control frames in the outbound queue.", constraints.status().message());
  EXPECT_EQ(1, stats_store_.counter("http2.outbound_control_flood").value());
  EXPECT_EQ(3,
            stats_store_
                .gauge("http2.outbound_control_frames_active", Stats::Gauge::ImportMode::Accumulate)
                .value());
}

TEST_F(ProtocolConstraintsTest, OutboundFrameFlood) {
  options_.mutable_max_outbound_frames()->set_value(5);
  options_.mutable_max_outbound_control_frames()->set_value(2);
  ProtocolConstraints constraints(http2CodecStats(), options_);
  constraints.incrementOutboundFrameCount(false);
  constraints.incrementOutboundFrameCount(false);
  constraints.incrementOutboundFrameCount(false);
  EXPECT_TRUE(constraints.checkOutboundFrameLimits().ok());
  constraints.incrementOutboundFrameCount(false);
  constraints.incrementOutboundFrameCount(false);
  constraints.incrementOutboundFrameCount(false);
  EXPECT_FALSE(constraints.checkOutboundFrameLimits().ok());
  EXPECT_TRUE(isBufferFloodError(constraints.status()));
  EXPECT_EQ("Too many frames in the outbound queue.", constraints.status().message());
  EXPECT_EQ(1, stats_store_.counter("http2.outbound_flood").value());
  EXPECT_EQ(6,
            stats_store_.gauge("http2.outbound_frames_active", Stats::Gauge::ImportMode::Accumulate)
                .value());
}

// Verify that the `status()` method reflects the first violation and is not modified by subsequent
// violations of outbound flood limits
TEST_F(ProtocolConstraintsTest, OutboundFrameFloodStatusIsIdempotent) {
  options_.mutable_max_outbound_frames()->set_value(5);
  options_.mutable_max_outbound_control_frames()->set_value(2);
  ProtocolConstraints constraints(http2CodecStats(), options_);
  // First trigger control frame flood
  constraints.incrementOutboundFrameCount(true);
  constraints.incrementOutboundFrameCount(true);
  constraints.incrementOutboundFrameCount(true);
  EXPECT_TRUE(isBufferFloodError(constraints.checkOutboundFrameLimits()));
  EXPECT_EQ("Too many control frames in the outbound queue.", constraints.status().message());
  // Then trigger flood check for all frame types
  constraints.incrementOutboundFrameCount(false);
  constraints.incrementOutboundFrameCount(false);
  constraints.incrementOutboundFrameCount(false);
  EXPECT_FALSE(constraints.checkOutboundFrameLimits().ok());
  EXPECT_TRUE(isBufferFloodError(constraints.status()));
  // The status should still reflect the first violation
  EXPECT_EQ("Too many control frames in the outbound queue.", constraints.status().message());
  EXPECT_EQ(1, stats_store_.counter("http2.outbound_control_flood").value());
  EXPECT_EQ(0, stats_store_.counter("http2.outbound_flood").value());
}

TEST_F(ProtocolConstraintsTest, InboundZeroLenData) {
  options_.mutable_max_consecutive_inbound_frames_with_empty_payload()->set_value(2);
  ProtocolConstraints constraints(http2CodecStats(), options_);
  uint8_t type = NGHTTP2_DATA;
  size_t length = 0;
  uint8_t flags = 0;
  EXPECT_TRUE(constraints.trackInboundFrames(length, type, flags, 0).ok());
  EXPECT_TRUE(constraints.trackInboundFrames(length, type, flags, 0).ok());
  EXPECT_TRUE(
      isInboundFramesWithEmptyPayloadError(constraints.trackInboundFrames(length, type, flags, 0)));
  EXPECT_TRUE(isInboundFramesWithEmptyPayloadError(constraints.status()));
  EXPECT_EQ(1, stats_store_.counter("http2.inbound_empty_frames_flood").value());
}

// Verify that the `status()` method reflects the first violation and is not modified by subsequent
// violations of outbound or inbound flood limits
TEST_F(ProtocolConstraintsTest, OutboundAndInboundFrameFloodStatusIsIdempotent) {
  options_.mutable_max_outbound_frames()->set_value(5);
  options_.mutable_max_outbound_control_frames()->set_value(2);
  options_.mutable_max_consecutive_inbound_frames_with_empty_payload()->set_value(2);
  ProtocolConstraints constraints(http2CodecStats(), options_);
  // First trigger inbound frame flood
  uint8_t type = NGHTTP2_DATA;
  size_t length = 0;
  uint8_t flags = 0;
  EXPECT_TRUE(constraints.trackInboundFrames(length, type, flags, 0).ok());
  EXPECT_TRUE(constraints.trackInboundFrames(length, type, flags, 0).ok());
  EXPECT_TRUE(
      isInboundFramesWithEmptyPayloadError(constraints.trackInboundFrames(length, type, flags, 0)));

  // Then trigger outbound control flood
  constraints.incrementOutboundFrameCount(true);
  constraints.incrementOutboundFrameCount(true);
  constraints.incrementOutboundFrameCount(true);
  EXPECT_TRUE(isInboundFramesWithEmptyPayloadError(constraints.checkOutboundFrameLimits()));
  EXPECT_EQ(1, stats_store_.counter("http2.inbound_empty_frames_flood").value());
  EXPECT_EQ(0, stats_store_.counter("http2.outbound_control_flood").value());
}

TEST_F(ProtocolConstraintsTest, InboundZeroLenDataWithPadding) {
  options_.mutable_max_consecutive_inbound_frames_with_empty_payload()->set_value(2);
  ProtocolConstraints constraints(http2CodecStats(), options_);
  uint8_t type = NGHTTP2_DATA;
  size_t length = 8;
  uint8_t flags = 0;
  EXPECT_TRUE(constraints.trackInboundFrames(length, type, flags, 8).ok());
  EXPECT_TRUE(constraints.trackInboundFrames(length, type, flags, 8).ok());
  EXPECT_TRUE(
      isInboundFramesWithEmptyPayloadError(constraints.trackInboundFrames(length, type, flags, 8)));
  EXPECT_TRUE(isInboundFramesWithEmptyPayloadError(constraints.status()));
  EXPECT_EQ(1, stats_store_.counter("http2.inbound_empty_frames_flood").value());
}

TEST_F(ProtocolConstraintsTest, InboundZeroLenDataEndStreamResetCounter) {
  options_.mutable_max_consecutive_inbound_frames_with_empty_payload()->set_value(2);
  ProtocolConstraints constraints(http2CodecStats(), options_);
  uint8_t type = NGHTTP2_DATA;
  size_t length = 0;
  uint8_t flags = 0;
  EXPECT_TRUE(constraints.trackInboundFrames(length, type, flags, 0).ok());
  EXPECT_TRUE(constraints.trackInboundFrames(length, type, flags, 0).ok());
  flags = NGHTTP2_FLAG_END_STREAM;
  EXPECT_TRUE(constraints.trackInboundFrames(length, type, flags, 0).ok());
  flags = 0;
  EXPECT_TRUE(constraints.trackInboundFrames(length, type, flags, 0).ok());
  EXPECT_TRUE(constraints.trackInboundFrames(length, type, flags, 0).ok());
  EXPECT_TRUE(
      isInboundFramesWithEmptyPayloadError(constraints.trackInboundFrames(length, type, flags, 0)));
  EXPECT_TRUE(isInboundFramesWithEmptyPayloadError(constraints.status()));
  EXPECT_EQ(1, stats_store_.counter("http2.inbound_empty_frames_flood").value());
}

TEST_F(ProtocolConstraintsTest, Priority) {
  options_.mutable_max_inbound_priority_frames_per_stream()->set_value(2);
  ProtocolConstraints constraints(http2CodecStats(), options_);
  // Create one stream
  constraints.incrementOpenedStreamCount();

  size_t length = 5;
  uint8_t type = NGHTTP2_PRIORITY;
  uint8_t flags = 0;
  EXPECT_TRUE(constraints.trackInboundFrames(length, type, flags, 0).ok());
  EXPECT_TRUE(constraints.trackInboundFrames(length, type, flags, 0).ok());
  EXPECT_TRUE(constraints.trackInboundFrames(length, type, flags, 0).ok());
  EXPECT_TRUE(constraints.trackInboundFrames(length, type, flags, 0).ok());
  EXPECT_TRUE(isBufferFloodError(constraints.trackInboundFrames(length, type, flags, 0)));
  EXPECT_TRUE(isBufferFloodError(constraints.status()));
  EXPECT_EQ("Too many PRIORITY frames", constraints.status().message());
  EXPECT_EQ(1, stats_store_.counter("http2.inbound_priority_frames_flood").value());
}

TEST_F(ProtocolConstraintsTest, WindowUpdate) {
  options_.mutable_max_inbound_window_update_frames_per_data_frame_sent()->set_value(2);
  ProtocolConstraints constraints(http2CodecStats(), options_);
  // Create one stream
  constraints.incrementOpenedStreamCount();
  // Send 2 DATA frames
  constraints.incrementOutboundDataFrameCount();
  constraints.incrementOutboundDataFrameCount();

  // Per the `5 + 2 * (opened_streams_ +
  // max_inbound_window_update_frames_per_data_frame_sent_ * outbound_data_frames_`
  // formula 5 + 2 * (1 + 2 * 2) = 15 WINDOW_UPDATE frames should NOT fail constraint
  // check, but 16th should.
  size_t length = 4;
  uint8_t type = NGHTTP2_WINDOW_UPDATE;
  uint8_t flags = 0;
  for (uint32_t i = 0; i < 15; ++i) {
    EXPECT_TRUE(constraints.trackInboundFrames(length, type, flags, 0).ok());
  }
  EXPECT_TRUE(isBufferFloodError(constraints.trackInboundFrames(length, type, flags, 0)));
  EXPECT_TRUE(isBufferFloodError(constraints.status()));
  EXPECT_EQ("Too many WINDOW_UPDATE frames", constraints.status().message());
  EXPECT_EQ(1, stats_store_.counter("http2.inbound_window_update_frames_flood").value());
}

TEST_F(ProtocolConstraintsTest, DumpsStateWithoutAllocatingMemory) {
  std::array<char, 1024> buffer;
  OutputBufferStream ostream{buffer.data(), buffer.size()};
  ProtocolConstraints constraints(http2CodecStats(), options_);

  Stats::TestUtil::MemoryTest memory_test;
  constraints.dumpState(ostream, 0);
  EXPECT_EQ(memory_test.consumedBytes(), 0);
  EXPECT_THAT(ostream.contents(), HasSubstr("ProtocolConstraints "));
  EXPECT_THAT(
      ostream.contents(),
      HasSubstr(" outbound_frames_: 0, max_outbound_frames_: 0, outbound_control_frames_: 0, "
                "max_outbound_control_frames_: 0, consecutive_inbound_frames_with_empty_payload_: "
                "0, max_consecutive_inbound_frames_with_empty_payload_: 0, opened_streams_: 0, "
                "inbound_priority_frames_: 0, max_inbound_priority_frames_per_stream_: 0, "
                "inbound_window_update_frames_: 0, outbound_data_frames_: 0, "
                "max_inbound_window_update_frames_per_data_frame_sent_: 0"));
}

} // namespace Http2
} // namespace Http
} // namespace Envoy
