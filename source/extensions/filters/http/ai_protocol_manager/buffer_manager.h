#pragma once

#include <cstdint>
#include <memory>
#include <utility>

#include "envoy/buffer/buffer.h"
#include "envoy/common/pure.h"
#include "envoy/event/dispatcher.h"
#include "envoy/event/schedulable_cb.h"

#include "source/common/common/assert.h"
#include "source/common/common/logger.h"
#include "source/extensions/filters/http/ai_protocol_manager/external_buffer.h"

#include "absl/container/flat_hash_set.h"
#include "absl/functional/any_invocable.h"
#include "absl/status/status.h"
#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AiProtocolManager {

class BufferManager;

// Invoked once a replay() range has been fully injected into the filter chain (or on error). The
// caller can start to stream further sub-ranges or to terminate the stream.
using ReplayDoneCallback = absl::AnyInvocable<void(absl::Status)>;

// Replay-side resume notification. A BufferManager implements this and attaches itself to the
// bridge while it replays; the bridge invokes it once chain back-pressure has drained.
class ReplayResumeHandler {
public:
  virtual ~ReplayResumeHandler() = default;

  virtual void onReplayResumed() PURE;
};

// Path-agnostic view of the Envoy filter chain, and the owner of this path's flow control.
//
// Each path supplies a concrete adapter (see filter_chain_bridge.h) that maps the virtual methods
// onto the corresponding decoder/encoder callbacks. The bridge is shared by every BufferManager on
// the path and outlives all of them: data into the BufferManager instances is from the same source
// (either downstream or upstream), so the back-pressure is aggregated across BufferManager
// instances.
class FilterChainBridge : public Logger::Loggable<Logger::Id::filter> {
public:
  // `buffer_limit` is the chain's ingest high watermark (the configured decoder/encoder buffer
  // limit); 0 disables ingest flow control.
  explicit FilterChainBridge(uint32_t buffer_limit)
      : high_watermark_(buffer_limit), low_watermark_(buffer_limit / 2) {}

  virtual ~FilterChainBridge();

  // Registers/unregisters a BufferManager on this path so detachFromFilterChain() can detach all
  // active stores on stream teardown.
  void registerBufferManager(BufferManager& manager) { registered_managers_.insert(&manager); }

  void unregisterBufferManager(BufferManager& manager) { registered_managers_.erase(&manager); }

  // Dispatcher the external buffer should use for completion/watermark callbacks.
  virtual Event::Dispatcher& dispatcher() PURE;

  // Re-injects a replayed data frame into the filter chain. Always a non-terminal
  // frame: end-of-stream is the caller's concern (see ReplayDoneCallback).
  virtual void injectData(Buffer::Instance& data) PURE;

  // Fails the stream after an unrecoverable external-buffer error.
  virtual void onUnrecoverableError() PURE;

  // Reports bytes a BufferManager has accepted but not yet made durable. The bridge sums them
  // across BufferManager instances and pauses the data source once the total reaches
  // high_watermark_.
  void addUnacked(uint64_t bytes);

  // Reports bytes that have since become durable, resuming the data source once the total falls
  // to low_watermark_.
  void releaseUnacked(uint64_t bytes);

  // Move-only RAII token that charges `bytes` to `addUnacked()` on construction and releases them
  // via `releaseUnacked()` on destruction or `reset()`.
  class ScopedUnacked {
  public:
    ScopedUnacked() = default;
    ScopedUnacked(FilterChainBridge& bridge, uint64_t bytes) : bridge_(&bridge), bytes_(bytes) {
      if (bytes_ > 0) {
        bridge_->addUnacked(bytes_);
      }
    }
    ScopedUnacked(ScopedUnacked&& other) noexcept
        : bridge_(std::exchange(other.bridge_, nullptr)), bytes_(std::exchange(other.bytes_, 0)) {}
    ScopedUnacked& operator=(ScopedUnacked&& other) noexcept {
      if (this != &other) {
        reset();
        bridge_ = std::exchange(other.bridge_, nullptr);
        bytes_ = std::exchange(other.bytes_, 0);
      }
      return *this;
    }
    ScopedUnacked(const ScopedUnacked&) = delete;
    ScopedUnacked& operator=(const ScopedUnacked&) = delete;
    ~ScopedUnacked() { reset(); }

    void reset() {
      FilterChainBridge* bridge = std::exchange(bridge_, nullptr);
      const uint64_t bytes = std::exchange(bytes_, 0);
      if (bridge != nullptr && bytes > 0) {
        bridge->releaseUnacked(bytes);
      }
    }

  private:
    FilterChainBridge* bridge_{nullptr};
    uint64_t bytes_{0};
  };

  // True while the data source is paused.
  bool ingestPaused() const { return source_paused_; }

  // Sets the handler to notify when replay back-pressure drains, for the duration of one replay
  // range. A handler set while the chain is already backed up starts out paused. The slot holds
  // one handler because only one BufferManager on a path injects at a time.
  void setReplayHandler(ReplayResumeHandler& handler) {
    ASSERT(replay_handler_ == nullptr || replay_handler_ == &handler);
    replay_handler_ = &handler;
  }

  // Releases the slot, if `handler` still holds it.
  void clearReplayHandler(const ReplayResumeHandler& handler) {
    if (replay_handler_ == &handler) {
      replay_handler_ = nullptr;
    }
  }

  // Stops watermark delivery. Must be called on or before the filter's onDestroy(): FilterManager
  // destroys its watermark callback lists before it destroys the filters that registered with
  // them, so unsubscribing from the bridge's destructor is too late.
  void detachFromFilterChain();

  // True while the chain we replay into is backed up.
  bool replayPaused() const { return replay_high_watermark_count_ > 0; }

  // Bounds the chunks injected in one event-loop pass, across every BufferManager on the path.
  // Returns false once the budget is spent, at which point the caller must stop injecting and
  // resume on a later pass. A BufferManager that completes reads synchronously can otherwise drive
  // the read/inject loop back-to-back, replaying a whole payload in one pass and starving other
  // connections and timers on this worker. The budget also caps that loop's recursion depth.
  bool tryConsumeInjectBudget();

  // Refills the budget. Called by a BufferManager re-entering the inject loop on a later pass than
  // the one that spent it -- never on a same-pass continuation, which would defeat the cap.
  void resetInjectBudget();

protected:
  // Entry points the adapter calls from the path's Envoy watermark callbacks.
  void onAboveReplayWatermark();

  void onBelowReplayWatermark();

private:
  // Pushes ingest back-pressure toward the data source (the filter's own
  // write-buffer high/low watermark on this path).
  virtual void pauseSource() PURE;

  virtual void resumeSource() PURE;

  // Unsubscribes *this from the path's Envoy watermark callbacks. The adapter subscribes in its
  // constructor, for the bridge's whole life.
  virtual void unsubscribeReplayWatermarks() PURE;

  void updateIngestBackpressure();

  // Chunks that can be injected in one event-loop pass (see tryConsumeInjectBudget).
  // BufferManager's ReadChunkSize times this bounds the bytes injected per pass.
  static constexpr uint32_t InjectChunksPerIteration = 8;

  uint32_t inject_budget_used_{0};

  // Bytes accepted by any store on this path that are not yet durable.
  uint64_t unacked_{0};
  const uint32_t high_watermark_;
  const uint32_t low_watermark_;
  // Tracked so each crossing pauses or resumes the source exactly once.
  bool source_paused_{false};
  bool detached_{false};

  // Depth of unmatched replay high-watermark callbacks. The connection manager may raise the
  // watermark more than once (stream and connection), so replay resumes only when this returns to
  // zero.
  uint32_t replay_high_watermark_count_{0};
  ReplayResumeHandler* replay_handler_{nullptr};
  absl::flat_hash_set<BufferManager*> registered_managers_;
};
using FilterChainBridgePtr = std::unique_ptr<FilterChainBridge>;

// Owns the "offload the payload into an ExternalBuffer, then replay ranges of it
// back into the filter chain" pipeline, with flow control in both directions.
// Offload and replay are decoupled and the caller is in control: it streams the
// body in via onData(), marks it complete with endStream(), then replays whatever
// ranges it wants via replay(offset, length, done) -- one range at a time, each
// reporting completion through its callback. All interaction with the filter
// chain goes through the owned FilterChainBridge, so the manager is path-agnostic:
// the decode and encode paths each construct a BufferManager with the appropriate
// bridge.
//
// Flow control is enforced in both directions:
//   - Ingest: the manager serializes writes (at most one outstanding) and queues
//     the backlog itself, batching small frames into chunk-sized writes
//     (WriteFlushThreshold) so a store that keeps up does not get a stream of tiny
//     writes. It reports the not-yet-durable byte count (queued plus in-flight)
//     to the bridge, which pushes back on the data source when the backing store
//     cannot keep up. Because the manager owns the queue, the external buffer
//     needs no flow-control surface of its own.
//   - Replay: re-injected data is paced against the chain's back-pressure, which
//     the bridge tracks. We pause issuing reads/injects while the chain is backed
//     up and resume on the bridge's ReplayResumeHandler callback.
//
// TODO(penguingao): Decouple passive storage from active filter-chain replay/injection.
// Currently BufferManager combines a passive payload store (in-memory queue + ExternalBuffer)
// with a callback-driven injection state machine (replay/inject -> bridge_.injectData). When
// short-lived values like SseEvent own BufferManager instances, child-to-parent callback
// reentrancy during injectData() can cancel the parent coroutine while BufferManager methods
// are still on the stack, requiring shared_ptr/weak_from_this() lifetime pinning and bridge
// registration. Refactoring replay/injection into a coroutine loop on the stream writer while
// keeping SseEvent stores purely passive (exposing async readChunk()) will restore strict
// std::unique_ptr lexical ownership without callback reentrancy.
class BufferManager : public ReplayResumeHandler,
                      public std::enable_shared_from_this<BufferManager>,
                      Logger::Loggable<Logger::Id::filter> {
public:
  struct Config {
    // Bytes the manager will hold in memory before it creates an external buffer at all. A
    // payload that never crosses this does no storage IO: it is replayed straight out of the
    // ingest queue. Zero offloads from the first byte.
    //
    // This decides only whether storage is created. Once it is, the resident footprint is bounded
    // as before, by write batching and the bridge's ingest watermark.
    uint64_t max_in_memory_bytes{0};
  };

  // `bridge` is shared by every BufferManager on the path and must outlive them all.
  BufferManager(Config config, ExternalBufferFactory& buffer_factory, FilterChainBridge& bridge);

  // onDestroy() must run before destruction (see onDestroy()): it detaches the
  // manager so nothing touches a half-torn-down bridge/buffer.
  ~BufferManager() override { onDestroy(); }

  // Offloads `data` into the external buffer (batching small frames). The caller
  // holds the filter chain (returns StopIteration*) while the body is buffered.
  void onData(Buffer::Instance& data);

  // Signals that the full body has been offloaded, flushing any batched backlog to
  // the buffer so all of it becomes durable, and is ready to replay.
  void endStream();

  // Replays the byte range [offset, offset+length) back into the filter chain as
  // data frames, invoking `done` once the whole range has been injected. The
  // caller may stream further sub-ranges with another replay(). Only one replay may be in flight
  // at a time. The range must lie within length(). Must not be called after cancelReplay().
  //
  // Must follow endStream(): this reads bytes back, and a caller reads a payload it has finished
  // writing. The range is made durable first, so the caller need not wait for the offload itself.
  void replay(uint64_t offset, uint64_t length, ReplayDoneCallback done);

  // Emits caller-supplied `data` (e.g. the serializer's small buffer) into the filter chain as
  // data frames, pacing against watermark flow control and burst limits, invoking `done` once all
  // bytes have been drained. `data` is moved from. Must not be called after cancelReplay().
  //
  // Shares the one-operation-at-a-time slot with replay(), but reads nothing back, so unlike
  // replay() it may precede endStream() and does not touch the store at all.
  void inject(Buffer::Instance& data, ReplayDoneCallback done);

  // Total number of bytes offloaded so far (durable, queued, and in-flight). The
  // caller uses this to size replay ranges; it is final once endStream() has been
  // called.
  uint64_t length() const {
    return (buffer_ == nullptr ? 0 : buffer_->length()) + pending_.length() + in_flight_write_size_;
  }

  // True until the first onData().
  bool empty() const { return length() == 0; }

  // Every accepted byte, while the payload is still within the in-memory tier; nullptr once it has
  // been offloaded, from which point the bytes are reachable only through replay().
  const Buffer::Instance* inMemoryBytes() const { return buffer_ == nullptr ? &pending_ : nullptr; }

  // Cancels any in-flight or requested replay() or inject() and disarms callbacks.
  // Permanent: once cancelled, no further replay operations may be started on this manager.
  void cancelReplay();

  // Detaches the manager from the filter chain: releases the external buffer, releases the
  // bridge's replay handler slot, and cancels the pending replay continuation, so async
  // completions and replay reentrancy become inert (they early-out on destroyed_). Idempotent.
  // Re-entrant replay/inject methods pin a shared_ptr via weak_from_this() so dropping the
  // external BufferManagerPtr synchronously during injectData() or replay completion is safe.
  void onDestroy();

  // ReplayResumeHandler
  void onReplayResumed() override;

private:
  // Issues a write of the queued backlog when one is warranted: no write is in
  // flight and the backlog has reached WriteFlushThreshold, the stream has ended,
  // or the source is paused. Hands the whole backlog to the buffer as a single
  // write() (honoring the single-writer contract); anything that arrives
  // afterwards waits in pending_ for the next write.
  void maybeIssueWrite();

  // Completion handler for a write() issued by maybeIssueWrite(). Drains the next
  // queued write or, once the queue is empty and a replay has been requested,
  // begins it.
  void onWriteComplete(ExternalBufferStatus status);

  // True when every accepted byte can be read back right now: either the payload is
  // still in the in-memory tier, or the store holds all of it (nothing queued, no
  // write in flight).
  bool allBytesReadable() const;

  // Begins a requested replay, once every accepted byte is readable if it reads the store.
  // Idempotent. Starts replay synchronously, so it
  // must only be called from a context where injecting into the filter chain is
  // safe: a write completion or the scheduled continuation, never directly from
  // replay(), which can be called in decodeData or encodeData of a filter.
  void maybeStartReplay();

  // Ends the current replay range: clears the active flag and invokes the caller's
  // done callback (which may start the next range or terminate the stream).
  void finishReplay();

  // Issues the next replay read and injects it (via onReadComplete). A
  // synchronous store completes the read on-stack and re-enters here, chaining
  // chunks until the bridge's inject budget is spent, at which point it yields via
  // replay_cb_ and resumes next iteration. An asynchronous store drives one chunk
  // per completion and paces itself.
  void maybeReadNextChunk();

  // Drains the bytes handed to inject() into the filter chain, respecting watermark flow control.
  void maybeDrainInjected();

  // Target of replay_cb_. Runs off the caller's stack to either start replay
  // deferred from replay() (the caller may invoke it from a data callback, where
  // injecting reentrantly is unsafe), resume replay after the per-iteration chunk
  // budget was spent, or resume replay after chain back-pressure drained (deferred
  // out of the watermark callback, where injecting/erroring reentrantly is unsafe).
  void onReplayContinuation();

  // Completion handler for a read() issued during replay.
  void onReadComplete(ExternalBufferStatus status, Buffer::InstancePtr data);

  // replay_cb_, created on first use.
  Event::SchedulableCallback& replayCallback();

  // Fails the stream when an external-buffer operation errors out.
  void onExternalBufferError();

  // Size of each chunk streamed back to the filter chain during replay. Keeps
  // the replay footprint bounded regardless of total payload size.
  static constexpr uint64_t ReadChunkSize = 64 * 1024;
  // Minimum queued backlog that triggers a write while the stream is still open
  // and the source is flowing. Because only one write is outstanding at a time, a
  // store that keeps up would otherwise get one tiny write per arriving frame;
  // batching coalesces small frames into chunk-sized writes. The backlog is
  // flushed regardless of size at end_stream (nothing more is coming) or while the
  // source is paused for back-pressure (the batch cannot grow, so waiting would
  // stall -- this also guarantees progress when the buffer limit is below the
  // threshold).
  static constexpr uint64_t WriteFlushThreshold = ReadChunkSize;

  Config config_;
  ExternalBufferFactory& buffer_factory_;
  FilterChainBridge& bridge_;
  ExternalBufferPtr buffer_;
  // Reschedules replay work out of the current call stack: starts it when replay()
  // is requested after the offload is already durable (deferred to later in the
  // same event-loop pass so we never inject reentrantly from the caller's
  // context), resumes it on the next iteration once the per-iteration chunk
  // budget is spent (so a large replay cannot monopolize the worker), and resumes
  // it when chain back-pressure drains (deferred out of the watermark callback so
  // we never inject/error reentrantly from within it).
  Event::SchedulableCallbackPtr replay_cb_;

  // True once endStream() has been called; gates flushing the batched backlog (the
  // tail is written even if it is below WriteFlushThreshold).
  bool end_stream_seen_{false};
  // True once cancelReplay() has been called. Permanent: disables all subsequent replay attempts.
  bool replay_cancelled_{false};
  // True once replay() has been requested by the caller and not yet started.
  // Replay starts once this is set and the write queue has fully drained (no write
  // in flight, nothing pending).
  bool replay_requested_{false};
  // Ingest write queue. Accepted-but-not-yet-written bytes accumulate here; the
  // manager hands the whole backlog to the buffer as a single write() once the
  // previous write completes, enforcing the buffer's single-writer contract.
  Buffer::OwnedImpl pending_;
  // True while a write() is outstanding (between maybeIssueWrite() and its
  // onWriteComplete()). Prevents overlapping writes.
  bool write_in_flight_{false};
  // Byte length of the in-flight write, counted as not-yet-durable for ingest
  // back-pressure until onWriteComplete() fires. Zero when no write is in flight.
  uint64_t in_flight_write_size_{0};

  // Keep track of where the current in-progress replay is sourced from: replay() or inject().
  // None means there isn't an on going replay.
  enum class ReplaySource { None, ExternalBuffer, Injected };
  ReplaySource replay_source_{ReplaySource::None};
  Buffer::OwnedImpl inject_data_;

  // True while a replay range is actively being streamed (from maybeStartReplay()
  // until finishReplay()).
  bool replaying_{false};
  // True while a replay read() is outstanding; prevents overlapping reads. For a
  // synchronous store it is set then cleared within the same maybeReadNextChunk()
  // call; for an asynchronous store it stays set until the posted completion fires.
  bool read_in_flight_{false};
  // True only while inside the synchronous buffer_->read() call. A store that
  // completes the read on-stack re-enters maybeReadNextChunk() (via onReadComplete)
  // with this set -- that is how a synchronous burst is detected and bounded; an
  // asynchronous completion re-enters with it clear and so paces itself.
  bool in_read_{false};
  // True between yielding for a spent inject budget and the continuation that refills it, so only
  // that continuation refills -- a same-pass resume must not.
  bool budget_yielded_{false};
  // Cursor for the active replay range: next offset to read and the end offset
  // (the requested offset + length). Reading is done when replay_offset_ reaches
  // replay_end_.
  uint64_t replay_offset_{0};
  uint64_t replay_end_{0};
  // Invoked when the active replay range is fully injected; set by replay().
  ReplayDoneCallback replay_done_;

  // Detachment latch: set in onDestroy(), which releases the bridge and buffer but
  // not the manager itself (see onDestroy()), so the object stays alive-but-detached
  // until its owner frees it. The deferred entry points
  // (onWriteComplete/onReadComplete/onReplayContinuation) and maybeReadNextChunk()
  // cannot legitimately run once detached -- a conforming store cancels pending
  // completions, onDestroy() cancels replay_cb_, and replay never outlasts a detach
  // -- so they ASSERT(!destroyed_) rather than branch on it. The one place a detach
  // races live code is inside onReadComplete(), where injecting a replayed frame can
  // end the stream and detach us before the call returns; that single spot checks
  // destroyed_ at runtime and bails.
  bool destroyed_{false};
};

using BufferManagerPtr = std::shared_ptr<BufferManager>;

} // namespace AiProtocolManager
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
