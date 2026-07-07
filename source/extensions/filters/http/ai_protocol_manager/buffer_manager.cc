#include "source/extensions/filters/http/ai_protocol_manager/buffer_manager.h"

#include <algorithm>
#include <utility>

#include "source/common/buffer/buffer_impl.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AiProtocolManager {

BufferManager::BufferManager(ExternalBufferFactory& buffer_factory, FilterChainBridgePtr bridge)
    : buffer_factory_(buffer_factory), bridge_(std::move(bridge)) {
  // Reschedules replay work out of the current call stack: it starts a replay
  // deferred from replay() (later in this same event-loop pass, once the caller's
  // filter callback unwinds, so we avoid injecting reentrantly) and resumes
  // replay on the next iteration after the per-iteration chunk budget is spent
  // (see maybeReadNextChunk).
  replay_cb_ =
      bridge_->dispatcher().createSchedulableCallback([this]() { onReplayContinuation(); });
  // Subscribe to the path's replay watermarks so replay can be paced against
  // chain back-pressure. Note: subscribing may immediately deliver
  // high-watermark callbacks if the chain is already backed up; those only bump
  // the pause counter, which is safe here.
  bridge_->registerReplayWatermarks(*this);
}

void BufferManager::onDestroy() {
  destroyed_ = true;
  bridge_->unregisterReplayWatermarks();
  // Cancel any pending replay continuation so it cannot fire after teardown.
  replay_cb_->cancel();
  // Dropping the buffer cancels any pending write/read completion callbacks.
  buffer_.reset();
}

void BufferManager::onData(Buffer::Instance& data) {
  if (buffer_ == nullptr) {
    buffer_ = buffer_factory_.createBuffer(bridge_->dispatcher());
    // Bound the not-yet-durable payload to the configured buffer limit so the
    // resident footprint stays bounded; resume the source once it has drained to
    // half the limit.
    high_watermark_ = bridge_->bufferLimit();
    low_watermark_ = high_watermark_ / 2;
  }

  ENVOY_LOG(trace, "ai_protocol_manager: offloading {} bytes", data.length());
  // Queue the bytes, taking ownership now so the filter chain's buffer reference
  // does not dangle across the asynchronous offload. maybeIssueWrite() flushes the
  // queued backlog to the buffer as a single write once it is worth doing (batching
  // small frames up to WriteFlushThreshold). This ownership/serialization is
  // storage-agnostic, so it lives here once rather than in every ExternalBuffer.
  pending_.move(data);
  updateIngestBackpressure();
  maybeIssueWrite();
}

void BufferManager::endStream() {
  end_stream_seen_ = true;
  ENVOY_LOG(trace, "ai_protocol_manager: stream complete");
  // No more data is coming: flush whatever is batched, even below the threshold.
  maybeIssueWrite();
}

void BufferManager::replay(uint64_t offset, uint64_t length, ReplayDoneCallback done) {
  // One replay at a time: the caller chains sub-ranges from the done callback.
  ASSERT(!replaying_ && !replay_requested_);
  replay_offset_ = offset;
  replay_end_ = offset + length;
  replay_done_ = std::move(done);
  replay_requested_ = true;
  ENVOY_LOG(debug, "ai_protocol_manager: replay requested for [{}, {})", offset, replay_end_);
  // If the offload is already fully durable, the caller may be invoking us from a
  // filter data callback; defer the start so we do not inject into the chain
  // reentrantly. Current-iteration (the same primitive dispatcher.post() uses)
  // runs once this callback unwinds but still within this event-loop pass, matching
  // the in-flight-write path -- whose completion is delivered via post() -- and
  // avoiding an extra iteration of latency. If a write is still in flight, that
  // completion starts replay via maybeStartReplay() instead.
  if (!write_in_flight_ && pending_.length() == 0) {
    replay_cb_->scheduleCallbackCurrentIteration();
  }
}

void BufferManager::maybeIssueWrite() {
  // Honor the buffer's single-writer contract: only one write outstanding. The
  // rest of the backlog stays in pending_ until this one completes.
  if (write_in_flight_ || pending_.length() == 0) {
    return;
  }
  // Batch small frames into a chunk-sized write instead of writing each one: with
  // a single write in flight at a time, per-frame writes would stream many tiny
  // writes to the backing store. Durability is only needed before replay (which
  // waits for end_stream), so holding a sub-threshold backlog costs nothing on the
  // critical path. Flush early regardless of size once the stream has ended
  // (nothing more is coming) or the source is paused for back-pressure (the
  // backlog cannot grow, so waiting would stall -- this also guarantees progress
  // when the buffer limit is below the threshold).
  if (!end_stream_seen_ && !source_paused_ && pending_.length() < WriteFlushThreshold) {
    return;
  }
  auto owned = std::make_unique<Buffer::OwnedImpl>();
  in_flight_write_size_ = pending_.length();
  owned->move(pending_);
  write_in_flight_ = true;
  buffer_->write(std::move(owned),
                 [this](ExternalBufferStatus status) { onWriteComplete(status); });
}

void BufferManager::onWriteComplete(ExternalBufferStatus status) {
  // A conforming store cancels pending completions when it is destroyed in
  // onDestroy() (see ExternalBuffer), so this never fires once detached.
  ASSERT(!destroyed_);
  if (status != ExternalBufferStatus::Ok) {
    onExternalBufferError();
    return;
  }

  // The just-written bytes are now durable.
  write_in_flight_ = false;
  in_flight_write_size_ = 0;
  // Recompute back-pressure (the queue has shrunk) and let the source resume if
  // it has drained below the low watermark.
  updateIngestBackpressure();
  // Drain the next queued write, if any; replay waits until the queue empties.
  maybeIssueWrite();
  // Begin a requested replay only after the last byte has been offloaded.
  maybeStartReplay();
}

void BufferManager::maybeStartReplay() {
  // Start only once the caller has requested a replay and every accepted byte is
  // durable. Consumes the request; the range was set by replay().
  if (replaying_ || !replay_requested_ || write_in_flight_ || pending_.length() > 0) {
    return;
  }
  replay_requested_ = false;
  replaying_ = true;
  ENVOY_LOG(debug, "ai_protocol_manager: replaying [{}, {})", replay_offset_, replay_end_);
  maybeReadNextChunk();
}

void BufferManager::updateIngestBackpressure() {
  if (high_watermark_ == 0) {
    return; // Ingest flow control disabled.
  }
  // Not-yet-durable bytes: queued backlog plus the in-flight write.
  const uint64_t unacked = pending_.length() + in_flight_write_size_;
  if (!source_paused_ && unacked > high_watermark_) {
    source_paused_ = true;
    ENVOY_LOG(debug, "ai_protocol_manager: ingest high watermark ({} bytes not durable)", unacked);
    bridge_->pauseSource();
    return;
  }
  if (source_paused_ && unacked <= low_watermark_) {
    source_paused_ = false;
    ENVOY_LOG(debug, "ai_protocol_manager: ingest low watermark ({} bytes not durable)", unacked);
    bridge_->resumeSource();
  }
}

void BufferManager::maybeReadNextChunk() {
  // This is the only method that reads from buffer_, which onDestroy() releases.
  // Every path here runs while a replay is live, and a replay cannot outlast a
  // detach: onReadComplete() bails right after the inject that would detach us, and
  // the other callers assert !destroyed_ before reaching us. So buffer_ is always
  // valid below.
  ASSERT(!destroyed_);
  if (!replaying_ || read_in_flight_) {
    return;
  }
  // Pause while the chain we feed is backed up; onReplayBelowLowWatermark()
  // resumes once it drains. This bounds how much replayed data piles up in the
  // chain when it is slow.
  if (replay_high_watermark_count_ > 0) {
    ENVOY_LOG(trace, "ai_protocol_manager: replay paused at offset {} (chain back-pressure)",
              replay_offset_);
    return;
  }

  if (replay_offset_ >= replay_end_) {
    // The requested range is fully injected (reached immediately for a zero-length
    // range). Hand control back to the caller; it terminates the stream.
    finishReplay();
    return;
  }

  // Bound a synchronous burst. A store that completes the read on-stack re-enters
  // here (via onReadComplete) from within the buffer_->read() call below, with
  // in_read_ set; we chain such chunks only up to ReplayChunksPerIteration, then
  // yield so a fast store cannot replay the whole payload back-to-back and starve
  // other connections/timers on this worker. The budget also caps the recursion
  // depth. A non-reentrant entry (offload done, resume, or an asynchronous read
  // completion) restarts the burst -- an async store thus paces itself one chunk
  // per iteration and never reaches the cap.
  if (in_read_) {
    if (replay_sync_chunks_ >= ReplayChunksPerIteration) {
      ENVOY_LOG(trace, "ai_protocol_manager: replay yielding at offset {} after {} chunks",
                replay_offset_, replay_sync_chunks_);
      replay_cb_->scheduleCallbackNextIteration();
      return;
    }
    ++replay_sync_chunks_;
  } else {
    replay_sync_chunks_ = 1;
  }

  const uint64_t chunk = std::min(ReadChunkSize, replay_end_ - replay_offset_);
  read_in_flight_ = true;
  in_read_ = true;
  buffer_->read(replay_offset_, chunk,
                [this](ExternalBufferStatus status, Buffer::InstancePtr data) {
                  onReadComplete(status, std::move(data));
                });
  // A synchronous store completes this read on-stack (onReadComplete, and on the
  // final chunk the replay-done callback) before read() returns, which may detach us
  // via onDestroy(). Per the destruction contract we are only detached, never freed,
  // so this manager is still alive -- and clearing in_read_ on a detached manager is
  // harmless, so no destroyed_ guard is needed here (unlike onReadComplete, which
  // would go on to read another chunk from the released buffer).
  in_read_ = false;
}

void BufferManager::onReplayContinuation() {
  // onDestroy() cancels replay_cb_, so the continuation never fires once detached.
  ASSERT(!destroyed_);
  // Off the caller's stack: either start replay deferred from replay() (the
  // offload was already durable when the caller requested it), resume after the
  // per-iteration burst budget was spent, or resume after chain back-pressure
  // drained (deferred out of the watermark callback). maybeStartReplay() already
  // drives the first read, so dispatch on whether replay is underway to avoid
  // advancing a fresh burst twice.
  if (!replaying_) {
    maybeStartReplay();
  } else {
    maybeReadNextChunk();
  }
}

void BufferManager::onReadComplete(ExternalBufferStatus status, Buffer::InstancePtr data) {
  // A conforming store cancels pending completions on destruction, so we never enter
  // here already detached. (destroyed_ can still flip mid-method when the inject
  // below ends the stream -- handled by the runtime check after injectData().)
  ASSERT(!destroyed_);
  read_in_flight_ = false;
  if (status != ExternalBufferStatus::Ok) {
    onExternalBufferError();
    return;
  }

  replay_offset_ += data->length();
  // Inject even if a high watermark was raised while this read was in flight: at
  // most one extra chunk (ReadChunkSize) overshoots before we pause, which keeps
  // the overshoot bounded. Always a non-terminal frame; the caller terminates the
  // stream from the replay-done callback.
  //
  // injectData() re-enters the filter chain: a downstream filter may end the stream
  // synchronously (e.g. a local reply on a request-size limit), which detaches us
  // via onDestroy() on-stack. Per the destruction contract we are still alive here
  // (only the deferred free is pending), but detached. This runtime check stops us
  // before finishing the range (finishReplay() would run the caller's replay-done
  // callback into a torn-down stream) or reading another chunk from the released
  // buffer -- and it is what keeps maybeReadNextChunk()'s ASSERT(!destroyed_) valid.
  bridge_->injectData(*data);
  if (destroyed_) {
    return;
  }
  if (replay_offset_ >= replay_end_) {
    // Finish here rather than fall through to maybeReadNextChunk(): the inject
    // above may have raised the replay high watermark, and maybeReadNextChunk()
    // tests that pause before the end-of-range check, so it would stall instead of
    // completing a range that is already fully injected.
    finishReplay();
    return;
  }
  maybeReadNextChunk();
}

void BufferManager::finishReplay() {
  replaying_ = false;
  ENVOY_LOG(trace, "ai_protocol_manager: replay range [.., {}) complete", replay_end_);
  // Hand control back to the caller. Its callback may start the next sub-range or
  // terminate the stream; move the callback out first so it can re-arm replay().
  ReplayDoneCallback done = std::move(replay_done_);
  replay_done_ = nullptr;
  if (done) {
    done();
  }
}

void BufferManager::onExternalBufferError() {
  ENVOY_LOG(warn, "ai_protocol_manager: external buffer I/O error, failing stream");
  bridge_->onUnrecoverableError();
}

void BufferManager::onReplayAboveHighWatermark() {
  // May be called multiple times (stream and connection); count so we resume
  // only after a matching number of low-watermark callbacks.
  ++replay_high_watermark_count_;
  ENVOY_LOG(debug, "ai_protocol_manager: replay high watermark (depth={})",
            replay_high_watermark_count_);
}

void BufferManager::onReplayBelowLowWatermark() {
  ASSERT(replay_high_watermark_count_ > 0);
  --replay_high_watermark_count_;
  ENVOY_LOG(debug, "ai_protocol_manager: replay low watermark (depth={})",
            replay_high_watermark_count_);
  if (replay_high_watermark_count_ == 0) {
    // resume replay where we paused. Defer via replay_cb_ rather than reading which may potentially
    // error out or inject data synchronously. this is an Envoy watermark callback, and injecting a
    // chunk (or failing the stream on a buffer error) can re-enter the watermark callbacks during
    // router cleanup, which is unsafe. The continuation runs once this callback unwinds. If a
    // replay_cb_ was already scheduled (e.g. a per-iteration yield), this is idempotent.
    replay_cb_->scheduleCallbackCurrentIteration();
  }
}

} // namespace AiProtocolManager
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
