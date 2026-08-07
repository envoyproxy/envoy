#pragma once

#include <cstdint>
#include <memory>

#include "envoy/buffer/buffer.h"
#include "envoy/common/pure.h"
#include "envoy/event/dispatcher.h"
#include "envoy/event/schedulable_cb.h"

#include "source/common/common/assert.h"
#include "source/common/common/logger.h"
#include "source/extensions/filters/http/ai_protocol_manager/external_buffer.h"

#include "absl/functional/any_invocable.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AiProtocolManager {

// Invoked once a replay() range has been fully injected into the filter chain. The
// caller can start to stream further sub-ranges or to terminate the stream.
using ReplayDoneCallback = absl::AnyInvocable<void()>;

// Replay-side flow-control sink. The BufferManager implements this so a
// FilterChainBridge can forward the path-specific Envoy watermark callback
// (UpstreamWatermarkCallbacks on the decode path, DownstreamWatermarkCallbacks
// on the encode path) into the manager without the manager having to inherit
// either of those identically-named-but-distinct interfaces.
class ReplayWatermarkHandler {
public:
  virtual ~ReplayWatermarkHandler() = default;

  // The chain we replay into is backed up; pause issuing reads/injects.
  virtual void onReplayAboveHighWatermark() PURE;

  // The chain has drained; replay may resume.
  virtual void onReplayBelowLowWatermark() PURE;
};

// Path-agnostic view of the Envoy filter chain. The BufferManager talks only to
// this interface, so the same offload/replay logic serves both the decode and
// encode paths; each path supplies a concrete adapter (see filter_chain_bridge.h)
// that maps these methods onto the corresponding decoder/encoder callbacks.
//
// A bridge is owned by the BufferManager it serves, so it always outlives the
// manager's async callbacks.
class FilterChainBridge {
public:
  virtual ~FilterChainBridge() = default;

  // Dispatcher the external buffer should use for completion/watermark callbacks.
  virtual Event::Dispatcher& dispatcher() PURE;

  // High watermark for in-flight ingest data (the configured decoder/encoder
  // buffer limit).
  virtual uint32_t bufferLimit() PURE;

  // Re-injects a replayed data frame into the filter chain. Always a non-terminal
  // frame: end-of-stream is the caller's concern (see ReplayDoneCallback).
  virtual void injectData(Buffer::Instance& data) PURE;

  // Pushes ingest back-pressure toward the data source (the filter's own
  // write-buffer high/low watermark on this path).
  virtual void pauseSource() PURE;

  virtual void resumeSource() PURE;

  // Subscribes/unsubscribes `handler` to the path's replay back-pressure
  // (upstream watermarks on decode, downstream watermarks on encode).
  virtual void registerReplayWatermarks(ReplayWatermarkHandler& handler) PURE;

  virtual void unregisterReplayWatermarks() PURE;

  // Fails the stream after an unrecoverable external-buffer error.
  virtual void onUnrecoverableError() PURE;
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
//     writes. It tracks the not-yet-durable byte count (queued plus in-flight)
//     against the configured buffer limit and pushes back on the data source via
//     the bridge when the backing store cannot keep up. Because the manager owns
//     the queue, the external buffer needs no flow-control surface of its own.
//   - Replay: re-injected data is paced against the chain's back-pressure,
//     delivered through ReplayWatermarkHandler. We pause issuing reads/injects
//     while the chain is backed up.
class BufferManager : public ReplayWatermarkHandler, Logger::Loggable<Logger::Id::filter> {
public:
  BufferManager(ExternalBufferFactory& buffer_factory, FilterChainBridgePtr bridge);

  // onDestroy() must run before destruction (see onDestroy()): it detaches the
  // manager so nothing touches a half-torn-down bridge/buffer.
  ~BufferManager() override { ASSERT(destroyed_); }

  // Offloads `data` into the external buffer (batching small frames). The caller
  // holds the filter chain (returns StopIteration*) while the body is buffered.
  void onData(Buffer::Instance& data);

  // Signals that the full body has been offloaded, flushing any batched backlog to
  // the buffer so all of it becomes durable, and is ready to replay.
  void endStream();

  // Replays the byte range [offset, offset+length) back into the filter chain as
  // data frames, invoking `done` once the whole range has been injected. The
  // caller may stream further sub-ranges with another replay(). Only one replay may be in flight
  // at a time. The range must lie within length().
  //
  // Only call after endStream(). It may wait until all write is done before start streaming.
  void replay(uint64_t offset, uint64_t length, ReplayDoneCallback done);

  // Total number of bytes offloaded so far (durable, queued, and in-flight). The
  // caller uses this to size replay ranges; it is final once endStream() has been
  // called.
  uint64_t length() const {
    return (buffer_ == nullptr ? 0 : buffer_->length()) + pending_.length() + in_flight_write_size_;
  }

  // True until the first onData().
  bool empty() const {
    return buffer_ == nullptr || buffer_->length() + pending_.length() + in_flight_write_size_ == 0;
  }

  // Detaches the manager from the filter chain: releases the external buffer,
  // unsubscribes from replay watermarks, and cancels the pending replay
  // continuation, so async completions and replay reentrancy become inert (they
  // early-out on destroyed_). Must be called before the manager is destroyed, and
  // must NOT be followed by a synchronous destruction from within a replay callback:
  // onDestroy() can run on-stack while a replay is mid-inject (a downstream filter
  // may answer an injected frame with a stream-ending local reply), and the replay
  // machinery relies on the manager still being alive-but-detached when that inject
  // returns. Callers therefore detach here and free later (the owning filter frees
  // it at its own deferred destruction). Idempotent.
  void onDestroy();

  // ReplayWatermarkHandler (replay side: filter-chain back-pressure).
  void onReplayAboveHighWatermark() override;

  void onReplayBelowLowWatermark() override;

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

  // Begins the requested replay range if every accepted byte is durable (no write
  // in flight, nothing queued). Idempotent. Starts replay synchronously, so it
  // must only be called from a context where injecting into the filter chain is
  // safe: a write completion or the scheduled continuation, never directly from
  // replay(), which can be called in decodeData or encodeData of a filter.
  void maybeStartReplay();

  // Recomputes ingest back-pressure from the not-yet-durable byte count (queued
  // plus in-flight write) and pauses/resumes the data source via the bridge as
  // it crosses the high/low watermark.
  void updateIngestBackpressure();

  // Ends the current replay range: clears the active flag and invokes the caller's
  // done callback (which may start the next range or terminate the stream).
  void finishReplay();

  // Issues the next replay read and injects it (via onReadComplete). A
  // synchronous store completes the read on-stack and re-enters here, chaining
  // chunks until the burst hits ReplayChunksPerIteration, at which point it yields
  // via replay_cb_ and resumes next iteration. An asynchronous store drives one
  // chunk per completion and paces itself.
  void maybeReadNextChunk();

  // Target of replay_cb_. Runs off the caller's stack to either start replay
  // deferred from replay() (the caller may invoke it from a data callback, where
  // injecting reentrantly is unsafe), resume replay after the per-iteration chunk
  // budget was spent, or resume replay after chain back-pressure drained (deferred
  // out of the watermark callback, where injecting/erroring reentrantly is unsafe).
  void onReplayContinuation();

  // Completion handler for a read() issued during replay.
  void onReadComplete(ExternalBufferStatus status, Buffer::InstancePtr data);

  // Fails the stream when an external-buffer operation errors out.
  void onExternalBufferError();

  // Size of each chunk streamed back to the filter chain during replay. Keeps
  // the replay footprint bounded regardless of total payload size.
  static constexpr uint64_t ReadChunkSize = 64 * 1024;
  // Maximum number of chunks to replay in one synchronous burst before yielding
  // to the event loop. Only a store that completes reads synchronously (e.g. the
  // in-memory buffer) can drive the read->inject loop back-to-back; without a cap
  // it would replay the entire payload in one iteration, starving other
  // connections/timers on this worker. ReadChunkSize * this bounds the bytes
  // injected per iteration. A store that completes reads asynchronously paces
  // itself one chunk per completion and never reaches this cap.
  static constexpr uint32_t ReplayChunksPerIteration = 8;
  // Minimum queued backlog that triggers a write while the stream is still open
  // and the source is flowing. Because only one write is outstanding at a time, a
  // store that keeps up would otherwise get one tiny write per arriving frame;
  // batching coalesces small frames into chunk-sized writes. The backlog is
  // flushed regardless of size at end_stream (nothing more is coming) or while the
  // source is paused for back-pressure (the batch cannot grow, so waiting would
  // stall -- this also guarantees progress when the buffer limit is below the
  // threshold).
  static constexpr uint64_t WriteFlushThreshold = ReadChunkSize;

  ExternalBufferFactory& buffer_factory_;
  FilterChainBridgePtr bridge_;
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

  // Ingest back-pressure thresholds (bytes), derived from the configured buffer
  // limit. The source is paused when not-yet-durable bytes (pending_ plus the
  // in-flight write) exceed high_watermark_ and resumed once they fall to
  // low_watermark_. A high_watermark_ of 0 disables ingest flow control.
  uint32_t high_watermark_{0};
  uint32_t low_watermark_{0};
  // True while the data source is paused for ingest back-pressure; tracked so we
  // pause/resume the source exactly once per crossing.
  bool source_paused_{false};

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
  // Length of the current synchronous replay burst, capped at
  // ReplayChunksPerIteration. A fresh (non-reentrant) entry restarts it at 1; a
  // reentrant (synchronous) chunk increments it and yields once it hits the cap.
  uint32_t replay_sync_chunks_{0};
  // Cursor for the active replay range: next offset to read and the end offset
  // (the requested offset + length). Reading is done when replay_offset_ reaches
  // replay_end_.
  uint64_t replay_offset_{0};
  uint64_t replay_end_{0};
  // Invoked when the active replay range is fully injected; set by replay().
  ReplayDoneCallback replay_done_;

  // Depth of unmatched replay high-watermark callbacks. The connection manager
  // may raise the watermark more than once (stream and connection), so we resume
  // replay only when this returns to zero. Non-zero => replay paused.
  uint32_t replay_high_watermark_count_{0};

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

using BufferManagerPtr = std::unique_ptr<BufferManager>;

} // namespace AiProtocolManager
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
