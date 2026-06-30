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
  // Drives replay on a fresh event-loop iteration: starts it for an empty
  // terminal frame (see onData) and resumes it once the per-iteration chunk
  // budget is spent (see maybeReadNextChunk).
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

Http::FilterDataStatus BufferManager::onData(Buffer::Instance& data, bool end_stream) {
  if (buffer_ == nullptr) {
    buffer_ = buffer_factory_.createBuffer(bridge_->dispatcher());
    // Bound the not-yet-durable payload to the configured buffer limit so the
    // resident footprint stays bounded; resume the source once it has drained to
    // half the limit.
    high_watermark_ = bridge_->bufferLimit();
    low_watermark_ = high_watermark_ / 2;
  }

  end_stream_seen_ = end_stream;
  ENVOY_LOG(trace, "ai_protocol_manager: offloading {} bytes (end_stream={})", data.length(),
            end_stream);
  // Queue the bytes, taking ownership now so the filter chain's buffer reference
  // does not dangle across the asynchronous offload. maybeIssueWrite() flushes the
  // queued backlog to the buffer as a single write once it is worth doing (batching
  // small frames up to WriteFlushThreshold). This ownership/serialization is
  // storage-agnostic, so it lives here once rather than in every ExternalBuffer.
  pending_.move(data);
  updateIngestBackpressure();
  maybeIssueWrite();
  // A non-empty frame replays once its write completes (onWriteComplete, a posted
  // context). An empty terminal frame issues no write, so nothing would drive
  // replay; schedule it on a fresh event-loop iteration. We must not start replay
  // synchronously here -- injecting into the filter chain re-entrantly from within
  // the data callback is unsafe.
  if (end_stream_seen_ && !write_in_flight_ && pending_.length() == 0 && !replaying_) {
    replay_cb_->scheduleCallbackNextIteration();
  }

  // We own all buffering and continuation: hold the chain here and replay the
  // payload ourselves once it has been fully offloaded.
  return Http::FilterDataStatus::StopIterationNoBuffer;
}

Http::FilterTrailersStatus BufferManager::onTrailers() {
  // No body was offloaded (e.g. a headers + trailers request): there is nothing
  // to replay, so let the trailers flow normally.
  if (buffer_ == nullptr) {
    return Http::FilterTrailersStatus::Continue;
  }

  // The body ended without end_stream on a data frame; the trailers carry it.
  // Mark the stream complete so replay can begin, and replay the body ahead of
  // the trailers. The stream has ended, so flush any batched backlog now; if the
  // write queue is still draining, onWriteComplete() starts replay once it empties.
  trailers_pending_ = true;
  end_stream_seen_ = true;
  ENVOY_LOG(trace, "ai_protocol_manager: trailers observed; stream complete");
  maybeIssueWrite();
  maybeStartReplay();

  // Hold the trailers behind the replayed body; finishReplay() releases them via
  // bridge_->continueIteration() once the last body frame has been injected.
  return Http::FilterTrailersStatus::StopIteration;
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
  if (destroyed_) {
    return;
  }
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
  // Begin replay only after the last byte has been offloaded.
  maybeStartReplay();
}

void BufferManager::maybeStartReplay() {
  // Idempotent: replaying_ stays set through replay, and the conditions below are
  // only satisfiable before the first (and only) start.
  if (replaying_ || !end_stream_seen_ || write_in_flight_ || pending_.length() > 0) {
    return;
  }
  streamBackToFilterChain();
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
  } else if (source_paused_ && unacked <= low_watermark_) {
    source_paused_ = false;
    ENVOY_LOG(debug, "ai_protocol_manager: ingest low watermark ({} bytes not durable)", unacked);
    bridge_->resumeSource();
  }
}

void BufferManager::streamBackToFilterChain() {
  replaying_ = true;
  replay_offset_ = 0;
  replay_length_ = buffer_->length();
  ENVOY_LOG(debug, "ai_protocol_manager: replaying {} buffered bytes", replay_length_);
  maybeReadNextChunk();
}

void BufferManager::maybeReadNextChunk() {
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

  if (replay_offset_ >= replay_length_) {
    // Reached only for an empty payload; a non-empty payload finishes in
    // onReadComplete() when the last chunk is injected with end_stream. Emit an
    // empty end_stream marker unless trailers will carry stream completion.
    if (!trailers_pending_) {
      Buffer::OwnedImpl empty;
      bridge_->injectData(empty, true);
    }
    finishReplay();
    return;
  }

  // Bound a synchronous burst. A store that completes the read on-stack re-enters
  // here (via onReadComplete) from within the buffer_->read() call below, with
  // in_read_ set; we chain such chunks only up to ReplayChunksPerIteration, then
  // yield so a fast store cannot replay the whole payload back-to-back and starve
  // other connections/timers on this worker. The budget also caps the recursion
  // depth. A non-re-entrant entry (offload done, resume, or an asynchronous read
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

  const uint64_t chunk = std::min(ReadChunkSize, replay_length_ - replay_offset_);
  read_in_flight_ = true;
  in_read_ = true;
  buffer_->read(replay_offset_, chunk,
                [this](ExternalBufferStatus status, Buffer::InstancePtr data) {
                  onReadComplete(status, std::move(data));
                });
  in_read_ = false;
}

void BufferManager::onReplayContinuation() {
  if (destroyed_) {
    return;
  }
  // Fresh event-loop iteration. This fires either to start replay deferred from
  // onData (an empty terminal frame, which issues no write to drive it) or to
  // resume after the per-iteration burst budget was spent. maybeStartReplay() is
  // a no-op once replay is underway; maybeReadNextChunk() then makes progress
  // with a fresh budget.
  maybeStartReplay();
  maybeReadNextChunk();
}

void BufferManager::onReadComplete(ExternalBufferStatus status, Buffer::InstancePtr data) {
  if (destroyed_) {
    return;
  }
  read_in_flight_ = false;
  if (status != ExternalBufferStatus::Ok) {
    onExternalBufferError();
    return;
  }

  replay_offset_ += data->length();
  const bool last = replay_offset_ >= replay_length_;
  // Inject even if a high watermark was raised while this read was in flight:
  // at most one extra chunk (ReadChunkSize) overshoots before we pause, which
  // keeps the overshoot bounded. When trailers terminate the stream they carry
  // end_stream, so the final body frame must not.
  bridge_->injectData(*data, last && !trailers_pending_);
  if (last) {
    finishReplay();
    return;
  }
  maybeReadNextChunk();
}

void BufferManager::finishReplay() {
  replaying_ = false;
  if (trailers_pending_) {
    // The body has been fully replayed ahead of the trailers the connection
    // manager is holding; release them so they follow the body in order.
    ENVOY_LOG(trace, "ai_protocol_manager: replay complete; releasing trailers");
    bridge_->continueIteration();
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
    // Drained: resume replay where we paused.
    maybeReadNextChunk();
  }
}

} // namespace AiProtocolManager
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
