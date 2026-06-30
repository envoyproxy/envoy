#include <deque>
#include <memory>
#include <string>
#include <vector>

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
    // Simulate downstream back-pressure arising mid-replay: when configured, raise
    // the replay high watermark right after the Nth injected chunk, as a real
    // chain would when its write buffer fills.
    if (handler_ != nullptr && inject_calls_ == raise_replay_watermark_at_inject_) {
      handler_->onReplayAboveHighWatermark();
    }
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
  int raise_replay_watermark_at_inject_{0}; // 0 = never.
};

// An ExternalBuffer that completes both write and read asynchronously, via
// dispatcher.post() -- modelling a network/disk-backed store. Used to exercise
// the BufferManager's asynchronous (non-re-entrant) replay path, where each read
// completion runs on its own event-loop iteration.
class PostingExternalBuffer : public ExternalBuffer {
public:
  explicit PostingExternalBuffer(Event::Dispatcher& dispatcher) : dispatcher_(dispatcher) {}
  ~PostingExternalBuffer() override { *alive_ = false; }

  void write(Buffer::InstancePtr data, WriteCallback cb) override {
    // Enforce the single-writer contract: the manager must never issue a write
    // while another is outstanding.
    EXPECT_FALSE(write_active_);
    write_active_ = true;
    ++write_calls_;
    write_sizes_.push_back(data->length());
    dispatcher_.post([this, alive = alive_, data = std::move(data), cb = std::move(cb)]() mutable {
      if (!*alive) {
        return;
      }
      data_.move(*data);
      write_active_ = false;
      cb(ExternalBufferStatus::Ok);
    });
  }
  void read(uint64_t offset, uint64_t length, ReadCallback cb) override {
    auto out = std::make_unique<Buffer::OwnedImpl>();
    if (length > 0) {
      auto slice = std::make_unique<uint8_t[]>(length);
      data_.copyOut(offset, length, slice.get());
      out->add(slice.get(), length);
    }
    dispatcher_.post([alive = alive_, out = std::move(out), cb = std::move(cb)]() mutable {
      if (!*alive) {
        return;
      }
      cb(ExternalBufferStatus::Ok, std::move(out));
    });
  }
  uint64_t length() const override { return data_.length(); }

  // Number of write() calls and the byte length each carried, so tests can assert
  // that the manager serializes and coalesces queued writes.
  int write_calls_{0};
  std::vector<uint64_t> write_sizes_;

private:
  Event::Dispatcher& dispatcher_;
  Buffer::OwnedImpl data_;
  // True while a write's posted completion has not yet run; used to verify the
  // single-writer contract.
  bool write_active_{false};
  std::shared_ptr<bool> alive_{std::make_shared<bool>(true)};
};

class PostingExternalBufferFactory : public ExternalBufferFactory {
public:
  ExternalBufferPtr createBuffer(Event::Dispatcher& dispatcher) override {
    auto buffer = std::make_unique<PostingExternalBuffer>(dispatcher);
    last_ = buffer.get();
    return buffer;
  }

  // The most recently created buffer (owned by the BufferManager under test).
  PostingExternalBuffer* last_{nullptr};
};

class BufferManagerTest : public testing::Test {
public:
  BufferManagerTest() {
    // The in-memory buffer delivers write completions via dispatcher.post().
    // Capture those callbacks so the test can run the event loop deterministically.
    ON_CALL(dispatcher_, post(testing::_)).WillByDefault(Invoke([this](Event::PostCb cb) {
      posted_.push_back(std::move(cb));
    }));
    manager_ = makeManager(factory_);
  }

  // Builds a BufferManager backed by `factory`, wiring a fresh FakeBridge and a
  // MockSchedulableCallback (which the manager creates for replay yielding and
  // takes ownership of). Updates bridge_/replay_cb_ to point at the new ones.
  BufferManagerPtr makeManager(ExternalBufferFactory& factory) {
    auto bridge = std::make_unique<FakeBridge>(dispatcher_);
    bridge_ = bridge.get();
    replay_cb_ = new NiceMock<Event::MockSchedulableCallback>(&dispatcher_);
    return std::make_unique<BufferManager>(factory, std::move(bridge));
  }

  // Run all posted callbacks, including ones enqueued while draining.
  void drain() {
    while (!posted_.empty()) {
      Event::PostCb cb = std::move(posted_.front());
      posted_.pop_front();
      cb();
    }
  }

  NiceMock<Event::MockDispatcher> dispatcher_;
  std::deque<Event::PostCb> posted_;
  InMemoryExternalBufferFactory factory_;
  // Declared before manager_ so it outlives the manager that references it.
  PostingExternalBufferFactory posting_factory_;
  FakeBridge* bridge_{nullptr};
  // Owned by manager_; fire invokeCallback() to simulate the next event-loop
  // iteration resuming replay after a per-iteration budget yield.
  NiceMock<Event::MockSchedulableCallback>* replay_cb_{nullptr};
  BufferManagerPtr manager_;
};

// The body is offloaded chunk-by-chunk and replayed verbatim once end_stream is
// seen, with end_stream propagated on the final injected frame.
TEST_F(BufferManagerTest, OffloadsAndReplaysBody) {
  Buffer::OwnedImpl chunk1("{\"messages\":");
  EXPECT_EQ(manager_->onData(chunk1, false), Http::FilterDataStatus::StopIterationNoBuffer);
  Buffer::OwnedImpl chunk2("[\"hi\"]}");
  EXPECT_EQ(manager_->onData(chunk2, true), Http::FilterDataStatus::StopIterationNoBuffer);

  // Nothing has been replayed yet: writes and replay are all asynchronous.
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

// An empty terminal frame produces an empty end_stream marker downstream. Since
// it issues no write, replay is scheduled (not started re-entrantly from the data
// callback) and runs on the next event-loop iteration.
TEST_F(BufferManagerTest, EmptyBody) {
  Buffer::OwnedImpl empty;
  EXPECT_EQ(manager_->onData(empty, true), Http::FilterDataStatus::StopIterationNoBuffer);

  // Nothing injected synchronously; the replay continuation is scheduled.
  EXPECT_EQ(bridge_->inject_calls_, 0);
  EXPECT_TRUE(replay_cb_->enabled());

  replay_cb_->invokeCallback();
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

// A payload larger than the per-iteration replay budget (ReplayChunksPerIteration
// chunks of ReadChunkSize) is not replayed all at once: replay injects one budget
// of chunks, yields to the event loop, and resumes via the scheduled continuation.
// This keeps a large replay from monopolizing the worker thread.
TEST_F(BufferManagerTest, ReplayYieldsAcrossIterationsForLargePayload) {
  // 10 chunks total; the budget is 8, so replay should stop after 8 and resume
  // for the remaining 2 on the next iteration.
  const uint64_t chunk = 64 * 1024;
  const std::string big(10 * chunk, 'x');
  Buffer::OwnedImpl body(big);
  EXPECT_EQ(manager_->onData(body, true), Http::FilterDataStatus::StopIterationNoBuffer);

  // First iteration: exactly the budget of chunks is injected, then replay yields
  // (the continuation is scheduled, not posted) without completing the stream.
  drain();
  EXPECT_EQ(bridge_->inject_calls_, 8);
  EXPECT_EQ(bridge_->injected_.length(), 8 * chunk);
  EXPECT_FALSE(bridge_->injected_end_stream_);
  EXPECT_TRUE(replay_cb_->enabled());

  // The scheduled continuation (next event-loop iteration) resumes replay to
  // completion with the original bytes intact.
  replay_cb_->invokeCallback();
  drain();
  EXPECT_EQ(bridge_->inject_calls_, 10);
  EXPECT_TRUE(bridge_->injected_end_stream_);
  EXPECT_EQ(bridge_->injected_.toString(), big);
}

// A store that completes reads asynchronously (posts; like a network/disk
// backend) is not re-entrant, so the manager drives exactly one chunk per
// completion and never schedules an artificial yield -- the store paces itself.
// Even a payload larger than ReplayChunksPerIteration replays without a yield.
TEST_F(BufferManagerTest, AsyncStoreReplaysOneChunkPerCallWithoutYield) {
  manager_ = makeManager(posting_factory_);

  const std::string big(10 * 64 * 1024, 'x'); // 10 chunks > ReplayChunksPerIteration (8).
  Buffer::OwnedImpl body(big);
  EXPECT_EQ(manager_->onData(body, true), Http::FilterDataStatus::StopIterationNoBuffer);
  drain();

  EXPECT_FALSE(replay_cb_->enabled());
  EXPECT_EQ(bridge_->inject_calls_, 10);
  EXPECT_TRUE(bridge_->injected_end_stream_);
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

// Not-yet-durable bytes crossing the buffer limit pause the data source via the
// bridge; once the write completes and they drain, the source resumes.
TEST_F(BufferManagerTest, IngestBackpressureDrivesSourceFlowControl) {
  // Small limit so a single frame crosses the high watermark (high=100, low=50).
  bridge_->buffer_limit_ = 100;

  // The frame exceeds the high watermark. The in-memory write completion is
  // posted, so its bytes stay not-yet-durable until the event loop runs: the
  // source is paused immediately.
  Buffer::OwnedImpl body(std::string(150, 'x'));
  EXPECT_EQ(manager_->onData(body, false), Http::FilterDataStatus::StopIterationNoBuffer);
  EXPECT_EQ(bridge_->pause_source_calls_, 1);
  EXPECT_EQ(bridge_->resume_source_calls_, 0);

  // Draining makes the write durable; not-yet-durable bytes fall to zero (<= low)
  // and the source resumes.
  drain();
  EXPECT_EQ(bridge_->resume_source_calls_, 1);
}

// Sub-threshold frames are batched: each is held in the queue rather than written
// individually, and they coalesce into a single write flushed at end_stream.
TEST_F(BufferManagerTest, BatchesSmallFramesUntilEndStream) {
  manager_ = makeManager(posting_factory_);

  Buffer::OwnedImpl f1("aaa");
  Buffer::OwnedImpl f2("bbb");
  EXPECT_EQ(manager_->onData(f1, false), Http::FilterDataStatus::StopIterationNoBuffer);
  EXPECT_EQ(manager_->onData(f2, false), Http::FilterDataStatus::StopIterationNoBuffer);

  // The buffer is created lazily on the first onData(); neither small frame has
  // triggered a write -- they wait in the queue.
  PostingExternalBuffer* buffer = posting_factory_.last_;
  ASSERT_NE(buffer, nullptr);
  EXPECT_EQ(buffer->write_calls_, 0);

  Buffer::OwnedImpl f3("cc");
  EXPECT_EQ(manager_->onData(f3, true), Http::FilterDataStatus::StopIterationNoBuffer);

  // end_stream flushes the batched backlog as a single write.
  EXPECT_EQ(buffer->write_calls_, 1);
  EXPECT_THAT(buffer->write_sizes_, testing::ElementsAre(8)); // "aaa"+"bbb"+"cc".

  drain();
  EXPECT_TRUE(bridge_->injected_end_stream_);
  EXPECT_EQ(bridge_->injected_.toString(), "aaabbbcc");
}

// A frame at the flush threshold is written immediately; frames that arrive while
// that write is in flight queue and coalesce into a single follow-up write. The
// manager issues exactly one write at a time (asserted in the fake) and replays
// the reassembled payload verbatim.
TEST_F(BufferManagerTest, SerializesAndCoalescesQueuedWrites) {
  manager_ = makeManager(posting_factory_);

  // Each frame is exactly the flush threshold (WriteFlushThreshold == 64KiB), so
  // the first flushes at once.
  const uint64_t threshold = 64 * 1024;
  const std::string chunk(threshold, 'x');
  Buffer::OwnedImpl f1(chunk);
  Buffer::OwnedImpl f2(chunk);
  Buffer::OwnedImpl f3(chunk);
  EXPECT_EQ(manager_->onData(f1, false), Http::FilterDataStatus::StopIterationNoBuffer);
  EXPECT_EQ(manager_->onData(f2, false), Http::FilterDataStatus::StopIterationNoBuffer);
  EXPECT_EQ(manager_->onData(f3, true), Http::FilterDataStatus::StopIterationNoBuffer);

  // Only the first frame has been written; f2 and f3 wait behind the in-flight write.
  PostingExternalBuffer* buffer = posting_factory_.last_;
  ASSERT_NE(buffer, nullptr);
  EXPECT_EQ(buffer->write_calls_, 1);

  drain();

  // The queued backlog coalesced into one follow-up write -- two writes total,
  // never overlapping (asserted in the fake) -- and the payload round-trips.
  EXPECT_EQ(buffer->write_calls_, 2);
  EXPECT_THAT(buffer->write_sizes_, testing::ElementsAre(threshold, 2 * threshold));
  EXPECT_TRUE(bridge_->injected_end_stream_);
  EXPECT_EQ(bridge_->injected_.toString(), chunk + chunk + chunk);
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

  // Draining completes the write and starts replay, but replay is paused: no
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

// Back-pressure arising mid-replay (the chain fills as we inject) halts the
// synchronous read loop, and replay resumes when it clears.
TEST_F(BufferManagerTest, ReplayResumesMidStream) {
  const std::string big(200 * 1024, 'x'); // 4 chunks of 64KiB + remainder.
  // The chain raises its replay high watermark right after the first injected
  // chunk; the loop must then stop until it is released.
  bridge_->raise_replay_watermark_at_inject_ = 1;
  Buffer::OwnedImpl body(big);
  EXPECT_EQ(manager_->onData(body, true), Http::FilterDataStatus::StopIterationNoBuffer);

  // Append completes, replay injects one chunk, then pauses on the watermark
  // raised during that inject.
  drain();
  EXPECT_EQ(bridge_->inject_calls_, 1);
  EXPECT_FALSE(bridge_->injected_end_stream_);
  EXPECT_LT(bridge_->injected_.length(), big.size());

  // Release back-pressure; replay resumes synchronously to completion.
  ASSERT_NE(bridge_->handler_, nullptr);
  bridge_->handler_->onReplayBelowLowWatermark();
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
