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
  void injectData(Buffer::Instance& data) override {
    injected_.add(data);
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
  void onUnrecoverableError() override { ++error_calls_; }

  Event::Dispatcher& dispatcher_;
  uint32_t buffer_limit_{1024 * 1024};
  ReplayWatermarkHandler* handler_{nullptr};

  Buffer::OwnedImpl injected_;
  int inject_calls_{0};
  int pause_source_calls_{0};
  int resume_source_calls_{0};
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

// An ExternalBuffer that reports an I/O error from its write or read completion,
// to exercise the BufferManager's unrecoverable-error path. The write completion
// is posted (so the test drives it via the event loop); the read completes
// synchronously, like the in-memory store.
class FailingExternalBuffer : public ExternalBuffer {
public:
  enum class FailMode { Write, Read };
  FailingExternalBuffer(Event::Dispatcher& dispatcher, FailMode mode)
      : dispatcher_(dispatcher), mode_(mode) {}
  ~FailingExternalBuffer() override { *alive_ = false; }

  void write(Buffer::InstancePtr data, WriteCallback cb) override {
    dispatcher_.post([this, alive = alive_, data = std::move(data), cb = std::move(cb)]() mutable {
      if (!*alive) {
        return;
      }
      if (mode_ == FailMode::Write) {
        cb(ExternalBufferStatus::Error);
        return;
      }
      // A read-failure run still needs the bytes to become durable so replay can
      // begin and reach the failing read.
      data_.move(*data);
      cb(ExternalBufferStatus::Ok);
    });
  }
  void read(uint64_t offset, uint64_t length, ReadCallback cb) override {
    if (mode_ == FailMode::Read) {
      // On error `data` is nullptr (see external_buffer.h).
      cb(ExternalBufferStatus::Error, nullptr);
      return;
    }
    auto out = std::make_unique<Buffer::OwnedImpl>();
    if (length > 0) {
      auto slice = std::make_unique<uint8_t[]>(length);
      data_.copyOut(offset, length, slice.get());
      out->add(slice.get(), length);
    }
    cb(ExternalBufferStatus::Ok, std::move(out));
  }
  uint64_t length() const override { return data_.length(); }

private:
  Event::Dispatcher& dispatcher_;
  FailMode mode_;
  Buffer::OwnedImpl data_;
  std::shared_ptr<bool> alive_{std::make_shared<bool>(true)};
};

class FailingExternalBufferFactory : public ExternalBufferFactory {
public:
  explicit FailingExternalBufferFactory(FailingExternalBuffer::FailMode mode) : mode_(mode) {}
  ExternalBufferPtr createBuffer(Event::Dispatcher& dispatcher) override {
    return std::make_unique<FailingExternalBuffer>(dispatcher, mode_);
  }
  FailingExternalBuffer::FailMode mode_;
};

class BufferManagerTest : public testing::Test {
public:
  BufferManagerTest() {
    // The in-memory buffer delivers write completions via dispatcher.post().
    // Capture those callbacks so the test can run the event loop deterministically.
    ON_CALL(dispatcher_, post(testing::_)).WillByDefault(Invoke([this](Event::PostCb cb) {
      posted_.push_back(std::move(cb));
    }));
    resetManager(factory_);
  }

  // The manager requires onDestroy() before destruction (see buffer_manager.h);
  // detach the one still standing at end of test. Idempotent, so tests that already
  // called onDestroy() are fine.
  void TearDown() override {
    if (manager_ != nullptr) {
      manager_->onDestroy();
    }
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

  // Swaps in a manager backed by `factory`, detaching the outgoing one first so it
  // honors the onDestroy()-before-destruction contract when it is replaced.
  void resetManager(ExternalBufferFactory& factory) {
    if (manager_ != nullptr) {
      manager_->onDestroy();
    }
    manager_ = makeManager(factory);
  }

  // Replays the whole offloaded payload, recording completion in replay_done_.
  // (End-of-stream handling lives in the filter, not the manager, so the unit
  // tests just observe that the requested range was injected and done fired.)
  void replayAll() {
    manager_->replay(0, manager_->length(), [this]() { replay_done_ = true; });
  }

  // Run all posted callbacks, including ones enqueued while draining.
  void drain() {
    while (!posted_.empty()) {
      Event::PostCb cb = std::move(posted_.front());
      posted_.pop_front();
      cb();
    }
  }

  // Runs exactly one posted callback (FIFO), leaving the rest queued -- lets a test
  // pause with a write or read still in flight.
  void runOnePosted() {
    ASSERT_FALSE(posted_.empty());
    Event::PostCb cb = std::move(posted_.front());
    posted_.pop_front();
    cb();
  }

  NiceMock<Event::MockDispatcher> dispatcher_;
  std::deque<Event::PostCb> posted_;
  InMemoryExternalBufferFactory factory_;
  // Declared before manager_ so it outlives the manager that references it.
  PostingExternalBufferFactory posting_factory_;
  FailingExternalBufferFactory write_failing_factory_{FailingExternalBuffer::FailMode::Write};
  FailingExternalBufferFactory read_failing_factory_{FailingExternalBuffer::FailMode::Read};
  FakeBridge* bridge_{nullptr};
  // Owned by manager_; fire invokeCallback() to simulate the next event-loop
  // iteration resuming replay after a per-iteration budget yield, starting a
  // replay that was deferred because the offload was already durable, or resuming
  // replay after chain back-pressure clears (deferred out of the watermark
  // callback).
  NiceMock<Event::MockSchedulableCallback>* replay_cb_{nullptr};
  bool replay_done_{false};
  BufferManagerPtr manager_;
};

// The body is offloaded chunk-by-chunk and replayed verbatim once the caller
// triggers replay; completion is reported through the done callback.
TEST_F(BufferManagerTest, OffloadsAndReplaysBody) {
  Buffer::OwnedImpl chunk1("{\"messages\":");
  manager_->onData(chunk1);
  Buffer::OwnedImpl chunk2("[\"hi\"]}");
  manager_->onData(chunk2);
  manager_->endStream();
  replayAll();

  // Nothing has been replayed yet: writes and replay are all asynchronous.
  EXPECT_EQ(bridge_->inject_calls_, 0);
  EXPECT_FALSE(replay_done_);

  drain();

  EXPECT_GE(bridge_->inject_calls_, 1);
  EXPECT_TRUE(replay_done_);
  EXPECT_EQ(bridge_->injected_.toString(), "{\"messages\":[\"hi\"]}");
}

// A body that arrives in a single frame is still round-tripped.
TEST_F(BufferManagerTest, SingleFrameBody) {
  Buffer::OwnedImpl body("{}");
  manager_->onData(body);
  manager_->endStream();
  replayAll();
  drain();

  EXPECT_TRUE(replay_done_);
  EXPECT_EQ(bridge_->injected_.toString(), "{}");
}

// An empty body has nothing to inject: a zero-length replay still reports done so
// the caller can terminate the stream. With nothing offloaded the data is already
// durable, so replay is deferred to a fresh event-loop iteration.
TEST_F(BufferManagerTest, EmptyBody) {
  Buffer::OwnedImpl empty;
  manager_->onData(empty);
  manager_->endStream();
  replayAll();

  EXPECT_FALSE(replay_done_);
  EXPECT_TRUE(replay_cb_->enabled());

  replay_cb_->invokeCallback();
  EXPECT_TRUE(replay_done_);
  EXPECT_EQ(bridge_->inject_calls_, 0);
}

// A payload larger than the replay chunk size is streamed back in multiple
// bounded frames and reassembles to the original bytes.
TEST_F(BufferManagerTest, LargePayloadReplayedInChunks) {
  const std::string big(200 * 1024, 'x'); // > ReadChunkSize (64KiB)
  Buffer::OwnedImpl body(big);
  manager_->onData(body);
  manager_->endStream();
  replayAll();
  drain();

  EXPECT_GT(bridge_->inject_calls_, 1);
  EXPECT_TRUE(replay_done_);
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
  manager_->onData(body);
  manager_->endStream();
  replayAll();

  // First iteration: exactly the budget of chunks is injected, then replay yields
  // (the continuation is scheduled, not posted) without finishing.
  drain();
  EXPECT_EQ(bridge_->inject_calls_, 8);
  EXPECT_EQ(bridge_->injected_.length(), 8 * chunk);
  EXPECT_FALSE(replay_done_);
  EXPECT_TRUE(replay_cb_->enabled());

  // The scheduled continuation (next event-loop iteration) resumes replay to
  // completion with the original bytes intact.
  replay_cb_->invokeCallback();
  drain();
  EXPECT_EQ(bridge_->inject_calls_, 10);
  EXPECT_TRUE(replay_done_);
  EXPECT_EQ(bridge_->injected_.toString(), big);
}

// A store that completes reads asynchronously (posts; like a network/disk
// backend) is not re-entrant, so the manager drives exactly one chunk per
// completion and never schedules an artificial yield -- the store paces itself.
// Even a payload larger than ReplayChunksPerIteration replays without a yield.
TEST_F(BufferManagerTest, AsyncStoreReplaysOneChunkPerCallWithoutYield) {
  resetManager(posting_factory_);

  const std::string big(10 * 64 * 1024, 'x'); // 10 chunks > ReplayChunksPerIteration (8).
  Buffer::OwnedImpl body(big);
  manager_->onData(body);
  manager_->endStream();
  replayAll();
  drain();

  EXPECT_FALSE(replay_cb_->enabled());
  EXPECT_EQ(bridge_->inject_calls_, 10);
  EXPECT_TRUE(replay_done_);
  EXPECT_EQ(bridge_->injected_.toString(), big);
}

// Replay is caller-triggered: the body can be fully offloaded and durable before
// the caller decides to replay. replay() then starts playback on a fresh
// event-loop iteration, decoupling offload from playback.
TEST_F(BufferManagerTest, ReplayDeferredUntilCallerRequests) {
  Buffer::OwnedImpl body("{\"messages\":[\"hi\"]}");
  manager_->onData(body);
  manager_->endStream();

  // The write completes, but with no replay() request nothing is injected.
  drain();
  EXPECT_EQ(bridge_->inject_calls_, 0);

  // The caller decides to proceed. Replay is deferred to a fresh iteration (the
  // caller may be inside a data callback), so nothing is injected synchronously.
  replayAll();
  EXPECT_EQ(bridge_->inject_calls_, 0);
  EXPECT_TRUE(replay_cb_->enabled());

  replay_cb_->invokeCallback();
  EXPECT_GE(bridge_->inject_calls_, 1);
  EXPECT_TRUE(replay_done_);
  EXPECT_EQ(bridge_->injected_.toString(), "{\"messages\":[\"hi\"]}");
}

// The caller can stream the payload back as independent sub-ranges, chaining the
// next from each range's done callback. The ranges reassemble to the full payload.
TEST_F(BufferManagerTest, ReplaysSubRangesInSequence) {
  Buffer::OwnedImpl body("HELLOWORLD");
  manager_->onData(body);
  manager_->endStream();
  drain(); // Make the payload durable; replay not yet requested.
  EXPECT_EQ(bridge_->inject_calls_, 0);

  bool second_done = false;
  // Replay the first half, then chain the second half from its done callback.
  manager_->replay(0, 5, [&]() {
    EXPECT_EQ(bridge_->injected_.toString(), "HELLO");
    manager_->replay(5, 5, [&second_done]() { second_done = true; });
  });

  // First range was deferred (offload already durable).
  ASSERT_TRUE(replay_cb_->enabled());
  replay_cb_->invokeCallback();
  EXPECT_FALSE(second_done);

  // The chained second range is itself deferred; fire it to completion.
  ASSERT_TRUE(replay_cb_->enabled());
  replay_cb_->invokeCallback();
  EXPECT_TRUE(second_done);
  EXPECT_EQ(bridge_->injected_.toString(), "HELLOWORLD");
}

// Destroying the manager mid-flight cancels pending callbacks; no replay occurs.
TEST_F(BufferManagerTest, DestroyBeforeReplay) {
  Buffer::OwnedImpl body("payload");
  manager_->onData(body);
  manager_->endStream();
  replayAll();
  manager_->onDestroy();
  drain();

  EXPECT_EQ(bridge_->inject_calls_, 0);
  EXPECT_FALSE(replay_done_);
}

// A synchronous store completes its final replay read on-stack, so finishReplay()
// invokes the caller's done() from inside buffer_->read(). That callback may end
// the stream and detach the manager on-stack (onDestroy()) -- exactly what the real
// filter does when the terminal end-of-stream injection triggers teardown. Per the
// onDestroy()-before-destruction contract the manager is only detached here, not
// freed, so the read loop must unwind cleanly (guarding on destroyed_) rather than
// touch the released buffer/bridge. The actual free happens later (TearDown).
TEST_F(BufferManagerTest, TerminalReplayDoneDetachesManagerSynchronously) {
  Buffer::OwnedImpl body("payload");
  manager_->onData(body);
  manager_->endStream();

  std::string injected;
  bool done_ran = false;
  // The write is still in flight (posted) when replay() is requested, so replay
  // starts from the write completion during drain() and runs synchronously through
  // the in-memory store into this done callback.
  manager_->replay(0, manager_->length(), [&]() {
    done_ran = true;
    injected = bridge_->injected_.toString();
    // Mirror the filter's teardown: detach on-stack, mid-read.
    manager_->onDestroy();
  });

  drain();
  EXPECT_TRUE(done_ran);
  EXPECT_EQ(injected, "payload");
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

// With no body offloaded the manager reports empty(), so the caller knows there is
// nothing to replay (a trailer-only request lets its trailers flow without calling
// endStream()/replay()).
TEST_F(BufferManagerTest, EmptyWhenNoDataOffloaded) {
  EXPECT_TRUE(manager_->empty());
  Buffer::OwnedImpl body("x");
  manager_->onData(body);
  EXPECT_FALSE(manager_->empty());
}

// length() reports the total offloaded so far, including not-yet-durable bytes.
TEST_F(BufferManagerTest, LengthCountsNotYetDurableBytes) {
  Buffer::OwnedImpl chunk1("abc");
  manager_->onData(chunk1);
  Buffer::OwnedImpl chunk2("defg");
  manager_->onData(chunk2);
  manager_->endStream();
  // The writes are still in flight (posted), but length() already reflects them.
  EXPECT_EQ(manager_->length(), 7);
  drain();
  EXPECT_EQ(manager_->length(), 7);
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
  manager_->onData(body);
  EXPECT_EQ(bridge_->pause_source_calls_, 1);
  EXPECT_EQ(bridge_->resume_source_calls_, 0);

  // Draining makes the write durable; not-yet-durable bytes fall to zero (<= low)
  // and the source resumes.
  drain();
  EXPECT_EQ(bridge_->resume_source_calls_, 1);
}

// Sub-threshold frames are batched: each is held in the queue rather than written
// individually, and they coalesce into a single write flushed at endStream().
TEST_F(BufferManagerTest, BatchesSmallFramesUntilEndStream) {
  resetManager(posting_factory_);

  Buffer::OwnedImpl f1("aaa");
  Buffer::OwnedImpl f2("bbb");
  manager_->onData(f1);
  manager_->onData(f2);

  // The buffer is created lazily on the first onData(); neither small frame has
  // triggered a write -- they wait in the queue.
  PostingExternalBuffer* buffer = posting_factory_.last_;
  ASSERT_NE(buffer, nullptr);
  EXPECT_EQ(buffer->write_calls_, 0);

  Buffer::OwnedImpl f3("cc");
  manager_->onData(f3);
  manager_->endStream();

  // endStream() flushes the batched backlog as a single write.
  EXPECT_EQ(buffer->write_calls_, 1);
  EXPECT_THAT(buffer->write_sizes_, testing::ElementsAre(8)); // "aaa"+"bbb"+"cc".

  replayAll();
  drain();
  EXPECT_TRUE(replay_done_);
  EXPECT_EQ(bridge_->injected_.toString(), "aaabbbcc");
}

// A frame at the flush threshold is written immediately; frames that arrive while
// that write is in flight queue and coalesce into a single follow-up write. The
// manager issues exactly one write at a time (asserted in the fake) and replays
// the reassembled payload verbatim.
TEST_F(BufferManagerTest, SerializesAndCoalescesQueuedWrites) {
  resetManager(posting_factory_);

  // Each frame is exactly the flush threshold (WriteFlushThreshold == 64KiB), so
  // the first flushes at once.
  const uint64_t threshold = 64 * 1024;
  const std::string chunk(threshold, 'x');
  Buffer::OwnedImpl f1(chunk);
  Buffer::OwnedImpl f2(chunk);
  Buffer::OwnedImpl f3(chunk);
  manager_->onData(f1);
  manager_->onData(f2);
  manager_->onData(f3);
  manager_->endStream();

  // Only the first frame has been written; f2 and f3 wait behind the in-flight write.
  PostingExternalBuffer* buffer = posting_factory_.last_;
  ASSERT_NE(buffer, nullptr);
  EXPECT_EQ(buffer->write_calls_, 1);

  replayAll();
  drain();

  // The queued backlog coalesced into one follow-up write -- two writes total,
  // never overlapping (asserted in the fake) -- and the payload round-trips.
  EXPECT_EQ(buffer->write_calls_, 2);
  EXPECT_THAT(buffer->write_sizes_, testing::ElementsAre(threshold, 2 * threshold));
  EXPECT_TRUE(replay_done_);
  EXPECT_EQ(bridge_->injected_.toString(), chunk + chunk + chunk);
}

// When the chain we replay into is backed up before replay starts, no data is
// injected until the back-pressure is released.
TEST_F(BufferManagerTest, ReplayPausesUnderBackPressure) {
  const std::string big(200 * 1024, 'x'); // > ReadChunkSize, multiple chunks.
  Buffer::OwnedImpl body(big);
  manager_->onData(body);
  manager_->endStream();
  replayAll();

  // The chain signals back-pressure before replay begins.
  ASSERT_NE(bridge_->handler_, nullptr);
  bridge_->handler_->onReplayAboveHighWatermark();

  // Draining completes the write and starts replay, but replay is paused: no
  // chunk is read or injected while the high watermark is held.
  drain();
  EXPECT_EQ(bridge_->inject_calls_, 0);
  EXPECT_FALSE(replay_done_);

  // Releasing back-pressure schedules the resume off the watermark callback stack
  // (deferred so we never read/inject reentrantly from within it); firing the
  // continuation runs replay to completion.
  bridge_->handler_->onReplayBelowLowWatermark();
  ASSERT_TRUE(replay_cb_->enabled());
  replay_cb_->invokeCallback();
  drain();
  EXPECT_TRUE(replay_done_);
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
  manager_->onData(body);
  manager_->endStream();
  replayAll();

  // The write completes, replay injects one chunk, then pauses on the watermark
  // raised during that inject.
  drain();
  EXPECT_EQ(bridge_->inject_calls_, 1);
  EXPECT_FALSE(replay_done_);
  EXPECT_LT(bridge_->injected_.length(), big.size());

  // Release back-pressure; the resume is scheduled off the watermark callback
  // stack (deferred to avoid reentrant read/inject) and the continuation runs
  // replay to completion.
  ASSERT_NE(bridge_->handler_, nullptr);
  bridge_->handler_->onReplayBelowLowWatermark();
  ASSERT_TRUE(replay_cb_->enabled());
  replay_cb_->invokeCallback();
  EXPECT_TRUE(replay_done_);
  EXPECT_EQ(bridge_->injected_.toString(), big);
}

// High watermark callbacks can nest (stream + connection); replay resumes only
// after a matching number of low-watermark callbacks.
TEST_F(BufferManagerTest, NestedWatermarksRequireBalancedRelease) {
  const std::string big(200 * 1024, 'x');
  Buffer::OwnedImpl body(big);
  manager_->onData(body);
  manager_->endStream();
  replayAll();

  ASSERT_NE(bridge_->handler_, nullptr);
  bridge_->handler_->onReplayAboveHighWatermark();
  bridge_->handler_->onReplayAboveHighWatermark();
  drain();
  EXPECT_EQ(bridge_->inject_calls_, 0);

  // One release is not enough to resume.
  bridge_->handler_->onReplayBelowLowWatermark();
  drain();
  EXPECT_EQ(bridge_->inject_calls_, 0);
  EXPECT_FALSE(replay_done_);

  // Balanced release schedules the resume off the watermark callback stack; the
  // continuation runs replay to completion.
  bridge_->handler_->onReplayBelowLowWatermark();
  ASSERT_TRUE(replay_cb_->enabled());
  replay_cb_->invokeCallback();
  drain();
  EXPECT_TRUE(replay_done_);
  EXPECT_EQ(bridge_->injected_.toString(), big);
}

// A write that fails fails the stream: the manager surfaces the unrecoverable
// error through the bridge and injects nothing. A pending replay never starts.
TEST_F(BufferManagerTest, WriteErrorFailsStream) {
  resetManager(write_failing_factory_);

  Buffer::OwnedImpl body("payload");
  manager_->onData(body);
  manager_->endStream();
  replayAll();

  // The failing completion is posted; nothing has happened yet.
  EXPECT_EQ(bridge_->error_calls_, 0);

  drain();

  EXPECT_EQ(bridge_->error_calls_, 1);
  EXPECT_EQ(bridge_->inject_calls_, 0);
  EXPECT_FALSE(replay_done_);
}

// A read that fails during replay fails the stream: the manager reports the error
// through the bridge and does not finish the replay or inject the failed chunk.
TEST_F(BufferManagerTest, ReadErrorFailsStream) {
  resetManager(read_failing_factory_);

  Buffer::OwnedImpl body("payload");
  manager_->onData(body);
  manager_->endStream();
  replayAll();

  // Draining makes the write durable and starts replay, whose first read fails.
  drain();

  EXPECT_EQ(bridge_->error_calls_, 1);
  EXPECT_EQ(bridge_->inject_calls_, 0);
  EXPECT_FALSE(replay_done_);
}

// An external-buffer error delivered after onDestroy() is inert: the completion
// handlers bail out and never touch the (detached) bridge.
TEST_F(BufferManagerTest, ErrorAfterDestroyIsInert) {
  resetManager(write_failing_factory_);

  Buffer::OwnedImpl body("payload");
  manager_->onData(body);
  manager_->endStream();
  replayAll();
  manager_->onDestroy();

  // The posted write completion (whether it carried Ok or Error) is dropped once
  // the buffer is reset in onDestroy(); no error reaches the bridge.
  drain();
  EXPECT_EQ(bridge_->error_calls_, 0);
}

// With an asynchronous store, a replay resumed by a watermark drain while a read is
// still in flight must not issue an overlapping read -- it waits for the in-flight
// read to complete.
TEST_F(BufferManagerTest, ResumeSkipsReadWhileOneInFlight) {
  resetManager(posting_factory_);

  const std::string big(200 * 1024, 'x'); // Multiple chunks.
  Buffer::OwnedImpl body(big);
  manager_->onData(body);
  manager_->endStream();
  replayAll();

  // Run just the write completion: replay starts and posts its first read (now in
  // flight, not yet completed).
  runOnePosted();

  // A watermark cycle schedules a resume while the read is still in flight.
  ASSERT_NE(bridge_->handler_, nullptr);
  bridge_->handler_->onReplayAboveHighWatermark();
  bridge_->handler_->onReplayBelowLowWatermark();
  ASSERT_TRUE(replay_cb_->enabled());
  // The continuation finds a read in flight and returns without issuing another.
  replay_cb_->invokeCallback();

  // Draining the in-flight read (and its successors) still completes the replay
  // with the payload intact -- no read was dropped or duplicated.
  drain();
  EXPECT_TRUE(replay_done_);
  EXPECT_EQ(bridge_->injected_.toString(), big);
}

} // namespace
} // namespace AiProtocolManager
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
