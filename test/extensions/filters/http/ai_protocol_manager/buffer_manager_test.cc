#include <deque>
#include <memory>
#include <string>

#include "source/common/buffer/buffer_impl.h"
#include "source/extensions/filters/http/ai_protocol_manager/buffer_manager.h"
#include "source/extensions/filters/http/ai_protocol_manager/external_buffer_impl.h"

#include "test/mocks/event/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::Invoke;
using testing::NiceMock;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AiProtocolManager {
namespace {

// Hand-written FilterChainBridge that records everything the BufferManager does
// to the (notional) filter chain, so the path-agnostic offload/replay logic can
// be unit-tested without any HTTP filter mocks. Tests drive replay back-pressure
// through the captured ReplayWatermarkHandler, exactly as a real decoder/encoder
// bridge would when the connection manager raises a watermark.
class FakeBridge : public FilterChainBridge {
public:
  explicit FakeBridge(Event::Dispatcher& dispatcher) : dispatcher_(dispatcher) {}

  Event::Dispatcher& dispatcher() override { return dispatcher_; }
  uint32_t bufferLimit() override { return buffer_limit_; }
  void injectData(Buffer::Instance& data, bool end_stream) override {
    injected_.add(data);
    injected_end_stream_ = end_stream;
    ++inject_calls_;
  }
  void pauseSource() override { ++pause_source_calls_; }
  void resumeSource() override { ++resume_source_calls_; }
  void registerReplayWatermarks(ReplayWatermarkHandler& handler) override { handler_ = &handler; }
  void unregisterReplayWatermarks() override { handler_ = nullptr; }
  void continueIteration() override { ++continue_calls_; }
  void onUnrecoverableError() override { ++error_calls_; }

  Event::Dispatcher& dispatcher_;
  uint32_t buffer_limit_{1024 * 1024};
  ReplayWatermarkHandler* handler_{nullptr};

  Buffer::OwnedImpl injected_;
  bool injected_end_stream_{false};
  int inject_calls_{0};
  int pause_source_calls_{0};
  int resume_source_calls_{0};
  int continue_calls_{0};
  int error_calls_{0};
};

class BufferManagerTest : public testing::Test {
public:
  BufferManagerTest() {
    auto bridge = std::make_unique<FakeBridge>(dispatcher_);
    bridge_ = bridge.get();
    // The in-memory buffer delivers completions via dispatcher.post(). Capture
    // those callbacks so the test can run the event loop deterministically.
    ON_CALL(dispatcher_, post(testing::_)).WillByDefault(Invoke([this](Event::PostCb cb) {
      posted_.push_back(std::move(cb));
    }));
    manager_ = std::make_unique<BufferManager>(factory_, std::move(bridge));
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

  NiceMock<Event::MockDispatcher> dispatcher_;
  std::deque<Event::PostCb> posted_;
  InMemoryExternalBufferFactory factory_;
  FakeBridge* bridge_{nullptr};
  BufferManagerPtr manager_;
};

// The body is offloaded chunk-by-chunk and replayed verbatim once end_stream is
// seen, with end_stream propagated on the final injected frame.
TEST_F(BufferManagerTest, OffloadsAndReplaysBody) {
  Buffer::OwnedImpl chunk1("{\"messages\":");
  EXPECT_EQ(manager_->onData(chunk1, false), Http::FilterDataStatus::StopIterationNoBuffer);
  Buffer::OwnedImpl chunk2("[\"hi\"]}");
  EXPECT_EQ(manager_->onData(chunk2, true), Http::FilterDataStatus::StopIterationNoBuffer);

  // Nothing has been replayed yet: appends and replay are all asynchronous.
  EXPECT_EQ(bridge_->inject_calls_, 0);

  drain();

  EXPECT_GE(bridge_->inject_calls_, 1);
  EXPECT_TRUE(bridge_->injected_end_stream_);
  EXPECT_EQ(bridge_->injected_.toString(), "{\"messages\":[\"hi\"]}");
}

// A body that arrives in a single end_stream frame is still round-tripped.
TEST_F(BufferManagerTest, SingleFrameBody) {
  Buffer::OwnedImpl body("{}");
  EXPECT_EQ(manager_->onData(body, true), Http::FilterDataStatus::StopIterationNoBuffer);
  drain();

  EXPECT_TRUE(bridge_->injected_end_stream_);
  EXPECT_EQ(bridge_->injected_.toString(), "{}");
}

// An empty terminal frame produces an empty end_stream marker downstream.
TEST_F(BufferManagerTest, EmptyBody) {
  Buffer::OwnedImpl empty;
  EXPECT_EQ(manager_->onData(empty, true), Http::FilterDataStatus::StopIterationNoBuffer);
  drain();

  EXPECT_EQ(bridge_->inject_calls_, 1);
  EXPECT_TRUE(bridge_->injected_end_stream_);
  EXPECT_EQ(bridge_->injected_.length(), 0);
}

// A payload larger than the replay chunk size is streamed back in multiple
// bounded frames and reassembles to the original bytes.
TEST_F(BufferManagerTest, LargePayloadReplayedInChunks) {
  const std::string big(200 * 1024, 'x'); // > ReadChunkSize (64KiB)
  Buffer::OwnedImpl body(big);
  EXPECT_EQ(manager_->onData(body, true), Http::FilterDataStatus::StopIterationNoBuffer);
  drain();

  EXPECT_GT(bridge_->inject_calls_, 1);
  EXPECT_TRUE(bridge_->injected_end_stream_);
  EXPECT_EQ(bridge_->injected_.length(), big.size());
  EXPECT_EQ(bridge_->injected_.toString(), big);
}

// Destroying the manager mid-flight cancels pending callbacks; no replay occurs.
TEST_F(BufferManagerTest, DestroyBeforeReplay) {
  Buffer::OwnedImpl body("payload");
  EXPECT_EQ(manager_->onData(body, true), Http::FilterDataStatus::StopIterationNoBuffer);
  manager_->onDestroy();
  drain();

  EXPECT_EQ(bridge_->inject_calls_, 0);
}

// The manager subscribes to replay watermarks on construction so it can observe
// chain back-pressure during replay.
TEST_F(BufferManagerTest, RegistersReplayWatermarks) { EXPECT_NE(bridge_->handler_, nullptr); }

// onDestroy() unregisters the replay watermark subscription.
TEST_F(BufferManagerTest, DestroyUnregistersReplayWatermarks) {
  ASSERT_NE(bridge_->handler_, nullptr);
  manager_->onDestroy();
  EXPECT_EQ(bridge_->handler_, nullptr);
}

// Ingest back-pressure from the external buffer is forwarded to the source via
// the bridge.
TEST_F(BufferManagerTest, IngestWatermarksDriveSourceFlowControl) {
  manager_->onAboveHighWatermark();
  EXPECT_EQ(bridge_->pause_source_calls_, 1);
  EXPECT_EQ(bridge_->resume_source_calls_, 0);
  manager_->onBelowLowWatermark();
  EXPECT_EQ(bridge_->resume_source_calls_, 1);
}

// When the chain we replay into is backed up before replay starts, no data is
// injected until the back-pressure is released.
TEST_F(BufferManagerTest, ReplayPausesUnderBackPressure) {
  const std::string big(200 * 1024, 'x'); // > ReadChunkSize, multiple chunks.
  Buffer::OwnedImpl body(big);
  EXPECT_EQ(manager_->onData(body, true), Http::FilterDataStatus::StopIterationNoBuffer);

  // The chain signals back-pressure before replay begins.
  ASSERT_NE(bridge_->handler_, nullptr);
  bridge_->handler_->onReplayAboveHighWatermark();

  // Draining completes the append and starts replay, but replay is paused: no
  // chunk is read or injected while the high watermark is held.
  drain();
  EXPECT_EQ(bridge_->inject_calls_, 0);
  EXPECT_FALSE(bridge_->injected_end_stream_);

  // Releasing back-pressure resumes replay to completion.
  bridge_->handler_->onReplayBelowLowWatermark();
  drain();
  EXPECT_TRUE(bridge_->injected_end_stream_);
  EXPECT_EQ(bridge_->injected_.length(), big.size());
  EXPECT_EQ(bridge_->injected_.toString(), big);
}

// Back-pressure applied mid-replay halts further injection (bounded by one
// in-flight chunk of overshoot), and replay resumes when it clears.
TEST_F(BufferManagerTest, ReplayResumesMidStream) {
  const std::string big(200 * 1024, 'x'); // 4 chunks of 64KiB + remainder.
  Buffer::OwnedImpl body(big);
  EXPECT_EQ(manager_->onData(body, true), Http::FilterDataStatus::StopIterationNoBuffer);

  // Step the event loop: append completes, replay starts, first read is issued
  // and its chunk injected. Stop once at least one chunk has been injected.
  ASSERT_NE(bridge_->handler_, nullptr);
  while (bridge_->inject_calls_ == 0) {
    ASSERT_TRUE(drainOne());
  }
  const int injects_before_pause = bridge_->inject_calls_;
  EXPECT_FALSE(bridge_->injected_end_stream_);

  // Apply back-pressure, then fully drain. At most one already-in-flight read
  // may land after the watermark; replay then stalls (not complete).
  bridge_->handler_->onReplayAboveHighWatermark();
  drain();
  EXPECT_LE(bridge_->inject_calls_, injects_before_pause + 1);
  EXPECT_FALSE(bridge_->injected_end_stream_);
  EXPECT_LT(bridge_->injected_.length(), big.size());

  // Release and finish.
  bridge_->handler_->onReplayBelowLowWatermark();
  drain();
  EXPECT_TRUE(bridge_->injected_end_stream_);
  EXPECT_EQ(bridge_->injected_.toString(), big);
}

// High watermark callbacks can nest (stream + connection); replay resumes only
// after a matching number of low-watermark callbacks.
TEST_F(BufferManagerTest, NestedWatermarksRequireBalancedRelease) {
  const std::string big(200 * 1024, 'x');
  Buffer::OwnedImpl body(big);
  EXPECT_EQ(manager_->onData(body, true), Http::FilterDataStatus::StopIterationNoBuffer);

  ASSERT_NE(bridge_->handler_, nullptr);
  bridge_->handler_->onReplayAboveHighWatermark();
  bridge_->handler_->onReplayAboveHighWatermark();
  drain();
  EXPECT_EQ(bridge_->inject_calls_, 0);

  // One release is not enough to resume.
  bridge_->handler_->onReplayBelowLowWatermark();
  drain();
  EXPECT_EQ(bridge_->inject_calls_, 0);
  EXPECT_FALSE(bridge_->injected_end_stream_);

  // Balanced release resumes replay.
  bridge_->handler_->onReplayBelowLowWatermark();
  drain();
  EXPECT_TRUE(bridge_->injected_end_stream_);
  EXPECT_EQ(bridge_->injected_.toString(), big);
}

// When the stream is terminated by trailers (end_stream arrives on the trailers
// callback, not a data frame), the body is still replayed: the final body frame
// carries end_stream=false and the held trailers are released via the bridge
// once replay completes.
TEST_F(BufferManagerTest, TrailerTerminatedStreamReplaysBodyThenReleasesTrailers) {
  Buffer::OwnedImpl chunk1("{\"messages\":");
  EXPECT_EQ(manager_->onData(chunk1, false), Http::FilterDataStatus::StopIterationNoBuffer);
  Buffer::OwnedImpl chunk2("[\"hi\"]}");
  // Last body frame is NOT end_stream; the trailers will carry it.
  EXPECT_EQ(manager_->onData(chunk2, false), Http::FilterDataStatus::StopIterationNoBuffer);
  // Trailers arrive: iteration is held until the replayed body has been injected.
  EXPECT_EQ(manager_->onTrailers(), Http::FilterTrailersStatus::StopIteration);

  // Nothing replayed yet, and the trailers must not have been released.
  EXPECT_EQ(bridge_->inject_calls_, 0);
  EXPECT_EQ(bridge_->continue_calls_, 0);

  drain();

  // Body replayed verbatim, the final frame did NOT set end_stream, and the
  // trailers were released exactly once after the body.
  EXPECT_GE(bridge_->inject_calls_, 1);
  EXPECT_FALSE(bridge_->injected_end_stream_);
  EXPECT_EQ(bridge_->injected_.toString(), "{\"messages\":[\"hi\"]}");
  EXPECT_EQ(bridge_->continue_calls_, 1);
}

// A trailer-terminated request with no body has nothing to replay: onTrailers()
// returns Continue so the trailers flow normally, and nothing is injected.
TEST_F(BufferManagerTest, TrailersWithoutBodyContinue) {
  EXPECT_EQ(manager_->onTrailers(), Http::FilterTrailersStatus::Continue);
  drain();

  EXPECT_EQ(bridge_->inject_calls_, 0);
  EXPECT_EQ(bridge_->continue_calls_, 0);
}

// A large, multi-chunk body terminated by trailers replays in bounded frames,
// none of which set end_stream, and the trailers are released once after the
// last chunk.
TEST_F(BufferManagerTest, LargeTrailerTerminatedStream) {
  const std::string big(200 * 1024, 'x'); // > ReadChunkSize, multiple chunks.
  Buffer::OwnedImpl body(big);
  EXPECT_EQ(manager_->onData(body, false), Http::FilterDataStatus::StopIterationNoBuffer);
  EXPECT_EQ(manager_->onTrailers(), Http::FilterTrailersStatus::StopIteration);
  drain();

  EXPECT_GT(bridge_->inject_calls_, 1);
  EXPECT_FALSE(bridge_->injected_end_stream_);
  EXPECT_EQ(bridge_->injected_.toString(), big);
  EXPECT_EQ(bridge_->continue_calls_, 1);
}

// Trailers terminating an empty body still release the trailers (the empty body
// produces no injected frame, since the trailers carry stream completion).
TEST_F(BufferManagerTest, TrailersAfterEmptyBody) {
  Buffer::OwnedImpl empty;
  EXPECT_EQ(manager_->onData(empty, false), Http::FilterDataStatus::StopIterationNoBuffer);
  EXPECT_EQ(manager_->onTrailers(), Http::FilterTrailersStatus::StopIteration);
  drain();

  EXPECT_EQ(bridge_->inject_calls_, 0);
  EXPECT_EQ(bridge_->continue_calls_, 1);
}

} // namespace
} // namespace AiProtocolManager
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
