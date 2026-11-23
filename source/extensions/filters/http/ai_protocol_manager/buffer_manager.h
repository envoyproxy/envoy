#pragma once

#include <cstdint>
#include <memory>
#include <utility>

#include "envoy/buffer/buffer.h"
#include "envoy/event/schedulable_cb.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/assert.h"
#include "source/common/common/logger.h"
#include "source/extensions/filters/http/ai_protocol_manager/external_buffer.h"
#include "source/extensions/filters/http/ai_protocol_manager/filter_chain_bridge.h"

#include "absl/functional/any_invocable.h"
#include "absl/status/status.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AiProtocolManager {

// Invoked once a replay() range has been fully injected into the filter chain (or on error). The
// caller can start to stream further sub-ranges or to terminate the stream.
using ReplayDoneCallback = absl::AnyInvocable<void(absl::Status)>;

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
class BufferManager : public std::enable_shared_from_this<BufferManager>,
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
  ~BufferManager() { onDestroy(); }

  // Offloads `data` into the external buffer (batching small frames). The caller
  // holds the filter chain (returns StopIteration*) while the body is buffered.
  // `end_stream` marks `data` as the last frame of the body.
  void onData(Buffer::Instance& data, bool end_stream = false);

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

private:
  class BridgeAdapter : public FilterChainBridge::Client {
  public:
    explicit BridgeAdapter(BufferManager& owner) : owner_(owner) {}
    void onBridgeDetached() override { owner_.onDestroy(); }
    void onIngestPaused() override { owner_.maybeIssueWrite(); }

  private:
    BufferManager& owner_;
  };

  void onReplayResumed();
  void armWriteResumeCallback();
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
  BridgeAdapter bridge_adapter_{*this};
  ExternalBufferPtr buffer_;
  // Reschedules replay work out of the current call stack: starts it when replay()
  // is requested after the offload is already durable (deferred to later in the
  // same event-loop pass so we never inject reentrantly from the caller's
  // context), resumes it on the next iteration once the per-iteration chunk
  // budget is spent (so a large replay cannot monopolize the worker), and resumes
  // it when chain back-pressure drains (deferred out of the watermark callback so
  // we never inject/error reentrantly from within it).
  Event::SchedulableCallbackPtr replay_cb_;

  // True once the body has ended; gates flushing the batched backlog (the
  // tail is written even if it is below WriteFlushThreshold).
  bool end_stream_seen_{false};
  // True once a write carrying end_stream has been issued.
  bool end_stream_flushed_{false};
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
