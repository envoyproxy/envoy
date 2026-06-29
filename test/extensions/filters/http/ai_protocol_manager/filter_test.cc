#include <deque>
#include <memory>
#include <string>

#include "source/common/buffer/buffer_impl.h"
#include "source/extensions/filters/http/ai_protocol_manager/external_buffer_impl.h"
#include "source/extensions/filters/http/ai_protocol_manager/filter.h"

#include "test/mocks/http/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::Invoke;
using testing::NiceMock;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AiProtocolManager {
namespace {

class AiProtocolManagerFilterTest : public testing::Test {
public:
  AiProtocolManagerFilterTest() : filter_(factory_) {
    // Capture the upstream watermark callbacks the filter registers so tests can
    // simulate upstream back-pressure.
    ON_CALL(callbacks_, addUpstreamWatermarkCallbacks(testing::_))
        .WillByDefault(
            Invoke([this](Http::UpstreamWatermarkCallbacks& cb) { watermark_cb_ = &cb; }));
    ON_CALL(callbacks_, removeUpstreamWatermarkCallbacks(testing::_))
        .WillByDefault(
            Invoke([this](Http::UpstreamWatermarkCallbacks&) { watermark_cb_ = nullptr; }));
    filter_.setDecoderFilterCallbacks(callbacks_);
    // The in-memory buffer delivers completions via dispatcher.post(). Capture
    // those callbacks so the test can run the event loop deterministically.
    ON_CALL(callbacks_.dispatcher_, post(testing::_))
        .WillByDefault(Invoke([this](Event::PostCb cb) { posted_.push_back(std::move(cb)); }));
    ON_CALL(callbacks_, injectDecodedDataToFilterChain(testing::_, testing::_))
        .WillByDefault(Invoke([this](Buffer::Instance& data, bool end_stream) {
          injected_.add(data);
          injected_end_stream_ = end_stream;
          ++inject_calls_;
        }));
  }

  // Run all posted callbacks, including ones enqueued while draining.
  void drain() {
    while (!posted_.empty()) {
      Event::PostCb cb = std::move(posted_.front());
      posted_.pop_front();
      cb();
    }
  }

  // Run a single posted callback (FIFO); returns false if the queue was empty.
  bool drainOne() {
    if (posted_.empty()) {
      return false;
    }
    Event::PostCb cb = std::move(posted_.front());
    posted_.pop_front();
    cb();
    return true;
  }

  std::deque<Event::PostCb> posted_;
  InMemoryExternalBufferFactory factory_;
  NiceMock<Http::MockStreamDecoderFilterCallbacks> callbacks_;
  Http::UpstreamWatermarkCallbacks* watermark_cb_{};
  AiProtocolManagerFilter filter_;

  Buffer::OwnedImpl injected_;
  bool injected_end_stream_{false};
  int inject_calls_{0};
};

// The body is offloaded chunk-by-chunk and replayed verbatim once end_stream is
// seen, with end_stream propagated on the final injected frame.
TEST_F(AiProtocolManagerFilterTest, OffloadsAndReplaysBody) {
  Buffer::OwnedImpl chunk1("{\"messages\":");
  EXPECT_EQ(filter_.decodeData(chunk1, false), Http::FilterDataStatus::StopIterationNoBuffer);
  Buffer::OwnedImpl chunk2("[\"hi\"]}");
  EXPECT_EQ(filter_.decodeData(chunk2, true), Http::FilterDataStatus::StopIterationNoBuffer);

  // Nothing has been replayed yet: appends and replay are all asynchronous.
  EXPECT_EQ(inject_calls_, 0);

  drain();

  EXPECT_GE(inject_calls_, 1);
  EXPECT_TRUE(injected_end_stream_);
  EXPECT_EQ(injected_.toString(), "{\"messages\":[\"hi\"]}");
}

// A body that arrives in a single end_stream frame is still round-tripped.
TEST_F(AiProtocolManagerFilterTest, SingleFrameBody) {
  Buffer::OwnedImpl body("{}");
  EXPECT_EQ(filter_.decodeData(body, true), Http::FilterDataStatus::StopIterationNoBuffer);
  drain();

  EXPECT_TRUE(injected_end_stream_);
  EXPECT_EQ(injected_.toString(), "{}");
}

// An empty terminal frame produces an empty end_stream marker downstream.
TEST_F(AiProtocolManagerFilterTest, EmptyBody) {
  Buffer::OwnedImpl empty;
  EXPECT_EQ(filter_.decodeData(empty, true), Http::FilterDataStatus::StopIterationNoBuffer);
  drain();

  EXPECT_EQ(inject_calls_, 1);
  EXPECT_TRUE(injected_end_stream_);
  EXPECT_EQ(injected_.length(), 0);
}

// A payload larger than the replay chunk size is streamed back in multiple
// bounded frames and reassembles to the original bytes.
TEST_F(AiProtocolManagerFilterTest, LargePayloadReplayedInChunks) {
  const std::string big(200 * 1024, 'x'); // > ReadChunkSize (64KiB)
  Buffer::OwnedImpl body(big);
  EXPECT_EQ(filter_.decodeData(body, true), Http::FilterDataStatus::StopIterationNoBuffer);
  drain();

  EXPECT_GT(inject_calls_, 1);
  EXPECT_TRUE(injected_end_stream_);
  EXPECT_EQ(injected_.length(), big.size());
  EXPECT_EQ(injected_.toString(), big);
}

// Destroying the filter mid-flight cancels pending callbacks; no replay occurs.
TEST_F(AiProtocolManagerFilterTest, DestroyBeforeReplay) {
  Buffer::OwnedImpl body("payload");
  EXPECT_EQ(filter_.decodeData(body, true), Http::FilterDataStatus::StopIterationNoBuffer);
  filter_.onDestroy();
  drain();

  EXPECT_EQ(inject_calls_, 0);
}

// The filter registers for upstream watermark callbacks so it can observe
// upstream back-pressure during replay.
TEST_F(AiProtocolManagerFilterTest, RegistersUpstreamWatermarkCallbacks) {
  EXPECT_NE(watermark_cb_, nullptr);
}

// When the downstream chain (toward upstream) is backed up before replay starts,
// no data is injected until the back-pressure is released.
TEST_F(AiProtocolManagerFilterTest, ReplayPausesUnderUpstreamBackPressure) {
  const std::string big(200 * 1024, 'x'); // > ReadChunkSize, multiple chunks.
  Buffer::OwnedImpl body(big);
  EXPECT_EQ(filter_.decodeData(body, true), Http::FilterDataStatus::StopIterationNoBuffer);

  // Upstream signals back-pressure before replay begins.
  ASSERT_NE(watermark_cb_, nullptr);
  watermark_cb_->onAboveWriteBufferHighWatermark();

  // Draining completes the append and starts replay, but replay is paused: no
  // chunk is read or injected while the high watermark is held.
  drain();
  EXPECT_EQ(inject_calls_, 0);
  EXPECT_FALSE(injected_end_stream_);

  // Releasing back-pressure resumes replay to completion.
  watermark_cb_->onBelowWriteBufferLowWatermark();
  drain();
  EXPECT_TRUE(injected_end_stream_);
  EXPECT_EQ(injected_.length(), big.size());
  EXPECT_EQ(injected_.toString(), big);
}

// Back-pressure applied mid-replay halts further injection (bounded by one
// in-flight chunk of overshoot), and replay resumes when it clears.
TEST_F(AiProtocolManagerFilterTest, ReplayResumesMidStream) {
  const std::string big(200 * 1024, 'x'); // 4 chunks of 64KiB + remainder.
  Buffer::OwnedImpl body(big);
  EXPECT_EQ(filter_.decodeData(body, true), Http::FilterDataStatus::StopIterationNoBuffer);

  // Step the event loop: append completes, replay starts, first read is issued
  // and its chunk injected. Stop once at least one chunk has been injected.
  ASSERT_NE(watermark_cb_, nullptr);
  while (inject_calls_ == 0) {
    ASSERT_TRUE(drainOne());
  }
  const int injects_before_pause = inject_calls_;
  EXPECT_FALSE(injected_end_stream_);

  // Apply back-pressure, then fully drain. At most one already-in-flight read
  // may land after the watermark; replay then stalls (not complete).
  watermark_cb_->onAboveWriteBufferHighWatermark();
  drain();
  EXPECT_LE(inject_calls_, injects_before_pause + 1);
  EXPECT_FALSE(injected_end_stream_);
  EXPECT_LT(injected_.length(), big.size());

  // Release and finish.
  watermark_cb_->onBelowWriteBufferLowWatermark();
  drain();
  EXPECT_TRUE(injected_end_stream_);
  EXPECT_EQ(injected_.toString(), big);
}

// High watermark callbacks can nest (stream + connection); replay resumes only
// after a matching number of low-watermark callbacks.
TEST_F(AiProtocolManagerFilterTest, NestedWatermarksRequireBalancedRelease) {
  const std::string big(200 * 1024, 'x');
  Buffer::OwnedImpl body(big);
  EXPECT_EQ(filter_.decodeData(body, true), Http::FilterDataStatus::StopIterationNoBuffer);

  ASSERT_NE(watermark_cb_, nullptr);
  watermark_cb_->onAboveWriteBufferHighWatermark();
  watermark_cb_->onAboveWriteBufferHighWatermark();
  drain();
  EXPECT_EQ(inject_calls_, 0);

  // One release is not enough to resume.
  watermark_cb_->onBelowWriteBufferLowWatermark();
  drain();
  EXPECT_EQ(inject_calls_, 0);
  EXPECT_FALSE(injected_end_stream_);

  // Balanced release resumes replay.
  watermark_cb_->onBelowWriteBufferLowWatermark();
  drain();
  EXPECT_TRUE(injected_end_stream_);
  EXPECT_EQ(injected_.toString(), big);
}

} // namespace
} // namespace AiProtocolManager
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
