#include "source/extensions/filters/http/ai_protocol_manager/buffer_manager.h"

#include <algorithm>
#include <utility>

#include "source/common/buffer/buffer_impl.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AiProtocolManager {

FilterChainBridge::~FilterChainBridge() {
  detached_ = true;
  replay_handler_ = nullptr;
  // BufferManager::onDestroy() calls unregisterBufferManager(), erasing itself from
  // registered_managers_. Re-querying begin() on each iteration avoids holding an iterator across
  // flat_hash_set mutations.
  while (!registered_managers_.empty()) {
    (*registered_managers_.begin())->onDestroy();
  }
}

void FilterChainBridge::addUnacked(uint64_t bytes) {
  unacked_ += bytes;
  updateIngestBackpressure();
}

void FilterChainBridge::releaseUnacked(uint64_t bytes) {
  ASSERT(unacked_ >= bytes);
  unacked_ -= bytes;
  updateIngestBackpressure();
}

void FilterChainBridge::updateIngestBackpressure() {
  if (detached_ || high_watermark_ == 0) {
    return; // Detached or ingest flow control disabled.
  }
  if (!source_paused_ && unacked_ > high_watermark_) {
    source_paused_ = true;
    ENVOY_LOG(debug, "ai_protocol_manager: ingest high watermark ({} bytes not durable)", unacked_);
    pauseSource();
    return;
  }
  if (source_paused_ && unacked_ <= low_watermark_) {
    source_paused_ = false;
    ENVOY_LOG(debug, "ai_protocol_manager: ingest low watermark ({} bytes not durable)", unacked_);
    resumeSource();
  }
}

void FilterChainBridge::detachFromFilterChain() {
  if (!detached_) {
    detached_ = true;
    unsubscribeReplayWatermarks();
  }
  replay_handler_ = nullptr;
  // BufferManager::onDestroy() calls unregisterBufferManager(), erasing itself from
  // registered_managers_. Re-querying begin() on each iteration avoids holding an iterator across
  // flat_hash_set mutations.
  while (!registered_managers_.empty()) {
    (*registered_managers_.begin())->onDestroy();
  }
}

void FilterChainBridge::onAboveReplayWatermark() {
  ++replay_high_watermark_count_;
  ENVOY_LOG(debug, "ai_protocol_manager: replay high watermark (depth={})",
            replay_high_watermark_count_);
}

void FilterChainBridge::onBelowReplayWatermark() {
  ASSERT(replay_high_watermark_count_ > 0);
  --replay_high_watermark_count_;
  ENVOY_LOG(debug, "ai_protocol_manager: replay low watermark (depth={})",
            replay_high_watermark_count_);
  if (replay_high_watermark_count_ == 0 && replay_handler_ != nullptr) {
    replay_handler_->onReplayResumed();
  }
}

bool FilterChainBridge::tryConsumeInjectBudget() {
  if (inject_budget_used_ >= InjectChunksPerIteration) {
    return false;
  }
  ++inject_budget_used_;
  return true;
}

void FilterChainBridge::resetInjectBudget() { inject_budget_used_ = 0; }

BufferManager::BufferManager(Config config, ExternalBufferFactory& buffer_factory,
                             FilterChainBridge& bridge)
    : config_(config), buffer_factory_(buffer_factory), bridge_(bridge) {
  bridge_.registerBufferManager(*this);
}

Event::SchedulableCallback& BufferManager::replayCallback() {
  // Created on first use: a manager that never replays -- every SSE frame small enough to stay in
  // memory -- should not cost a dispatcher callback.
  if (replay_cb_ == nullptr) {
    replay_cb_ =
        bridge_.dispatcher().createSchedulableCallback([this]() { onReplayContinuation(); });
  }
  return *replay_cb_;
}

void BufferManager::onDestroy() {
  if (destroyed_) {
    return;
  }
  destroyed_ = true;
  bridge_.unregisterBufferManager(*this);
  // Cancel any requested or in-flight replay operation and disarm callbacks.
  cancelReplay();
  const bool had_buffer = (buffer_ != nullptr);
  const uint64_t unacked = had_buffer ? (in_flight_write_size_ + pending_.length()) : 0;
  in_flight_write_size_ = 0;
  pending_.drain(pending_.length());
  // Dropping the buffer cancels any pending async write/read completion callbacks.
  buffer_.reset();
  if (unacked > 0) {
    bridge_.releaseUnacked(unacked);
  }
}

void BufferManager::onData(Buffer::Instance& data) {
  const uint64_t accepted = data.length();
  // Queue the bytes, taking ownership now so the filter chain's buffer reference does not dangle
  // across the asynchronous offload. This ownership/serialization is storage-agnostic, so it lives
  // here once rather than in every ExternalBuffer.
  pending_.move(data);

  if (buffer_ == nullptr) {
    if (pending_.length() <= config_.max_in_memory_bytes) {
      // Still within the in-memory tier. These bytes are readable where they are, so they are not
      // pending durability and there is nothing to write.
      ENVOY_LOG(trace, "ai_protocol_manager: holding {} bytes in memory ({} total)", accepted,
                pending_.length());
      return;
    }
    ENVOY_LOG(debug, "ai_protocol_manager: offloading, {} bytes exceeds the in-memory limit of {}",
              pending_.length(), config_.max_in_memory_bytes);
    buffer_ = buffer_factory_.createBuffer(bridge_.dispatcher());
    // Everything held so far now needs a write to become readable, so all of it counts.
    bridge_.addUnacked(pending_.length());
    maybeIssueWrite();
    return;
  }

  ENVOY_LOG(trace, "ai_protocol_manager: offloading {} bytes", accepted);
  // maybeIssueWrite() flushes the queued backlog to the buffer as a single write once it is worth
  // doing (batching small frames up to WriteFlushThreshold).
  bridge_.addUnacked(accepted);
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
  if (replay_cancelled_ || replaying_ || replay_requested_) {
    IS_ENVOY_BUG("replay is in progress, or has been cancelled. currently only one pending replay "
                 "is supported. and if cancelReplay is called, no more replay is allowed.");
    done(absl::InternalError("replay is in progress, or has been cancelled"));
    return;
  }
  // Every range this reads is behind endStream(): a caller reads a payload it has finished
  // writing. Without that, ingest could move the range out of pending_ and into a write after the
  // replay had started, and the read would have to stall mid-range and be resumed.
  ASSERT(end_stream_seen_);
  replay_source_ = ReplaySource::ExternalBuffer;
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
  if (allBytesReadable()) {
    replayCallback().scheduleCallbackCurrentIteration();
  }
  // Otherwise the backlog endStream() flushed is still going down; onWriteComplete() starts the
  // replay through maybeStartReplay() once the last of it is readable.
}

void BufferManager::inject(Buffer::Instance& data, ReplayDoneCallback done) {
  // One operation at a time: the caller chains further spans from the done callback.
  if (replay_cancelled_ || replaying_ || replay_requested_) {
    IS_ENVOY_BUG("replay is in progress, or has been cancelled. currently only one pending replay "
                 "is supported. and if cancelReplay is called, no more replay is allowed.");
    done(absl::InternalError("replay is in progress, or has been cancelled"));
    return;
  }
  replay_source_ = ReplaySource::Injected;
  inject_data_.move(data);
  replay_done_ = std::move(done);
  replay_requested_ = true;
  ENVOY_LOG(debug, "ai_protocol_manager: inject requested for {} bytes", inject_data_.length());
  replayCallback().scheduleCallbackCurrentIteration();
}

void BufferManager::cancelReplay() {
  replay_cancelled_ = true;
  replay_requested_ = false;
  replaying_ = false;
  bridge_.clearReplayHandler(*this);
  replay_source_ = ReplaySource::None;
  replay_done_ = nullptr;
  inject_data_.drain(inject_data_.length());
  if (replay_cb_) {
    replay_cb_->cancel();
  }
}

bool BufferManager::allBytesReadable() const {
  // Within the in-memory tier every accepted byte is readable in place. Once a store exists, a
  // read may not be issued until the queue has drained into it: the store counts only acknowledged
  // writes, so a read past that point would be out of range.
  return buffer_ == nullptr || (!write_in_flight_ && pending_.length() == 0);
}

void BufferManager::maybeIssueWrite() {
  // Nothing to do while the payload is still within the in-memory tier: there is no store yet, and
  // the bytes are already readable in pending_.
  //
  // Honor the buffer's single-writer contract: only one write outstanding. The
  // rest of the backlog stays in pending_ until this one completes.
  if (buffer_ == nullptr || write_in_flight_ || pending_.length() == 0) {
    return;
  }
  // Batch small frames into a chunk-sized write instead of writing each one: with
  // a single write in flight at a time, per-frame writes would stream many tiny
  // writes to the backing store. Holding a sub-threshold backlog costs nothing on
  // the critical path as long as nothing needs those bytes durable yet. Flush early
  // regardless of size once the stream has ended (nothing more is coming) or the
  // source is paused for back-pressure (the backlog cannot grow, so waiting would
  // stall -- this also guarantees progress when the buffer limit is below the
  // threshold).
  if (!end_stream_seen_ && !bridge_.ingestPaused() && pending_.length() < WriteFlushThreshold) {
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
  // Asynchronous entry point (via ExternalBuffer::write callback capturing raw `this`).
  // Pinning `self` here keeps `*this` alive across any downstream stream reset or
  // coroutine cancellation triggered by onExternalBufferError() or maybeStartReplay().
  auto self = weak_from_this().lock();
  // A conforming store cancels pending completions when it is destroyed in
  // onDestroy() (see ExternalBuffer), so this never fires once detached.
  ASSERT(!destroyed_);
  const uint64_t acked = in_flight_write_size_;
  write_in_flight_ = false;
  in_flight_write_size_ = 0;
  // Let the source resume if the not-yet-durable total has fallen below the low
  // watermark.
  bridge_.releaseUnacked(acked);
  if (status != ExternalBufferStatus::Ok) {
    onExternalBufferError();
    return;
  }

  // Drain the next queued write, if any; replay waits until the queue empties.
  maybeIssueWrite();
  // Begin a requested replay only after the last byte has been offloaded. A replay that is already
  // running cannot be waiting on this write: a range replay starts only once every accepted byte
  // is readable, and endStream() has already ruled out any further ingest.
  maybeStartReplay();
}

void BufferManager::maybeStartReplay() {
  // Consumes the request; the range was set by replay().
  if (replay_cancelled_ || replaying_ || !replay_requested_) {
    return;
  }
  // A range replay reads the store, so it waits until every accepted byte is readable. An
  // inject carries the caller's own bytes and has nothing to wait for.
  if (replay_source_ == ReplaySource::ExternalBuffer && !allBytesReadable()) {
    return;
  }
  replay_requested_ = false;
  replaying_ = true;
  // Take the bridge's handler slot for the duration of the range: back-pressure only needs to
  // reach whoever is injecting, and only one manager on a path injects at a time.
  bridge_.setReplayHandler(*this);
  if (replay_source_ == ReplaySource::Injected) {
    maybeDrainInjected();
  } else if (replay_source_ == ReplaySource::ExternalBuffer) {
    ENVOY_LOG(debug, "ai_protocol_manager: replaying [{}, {})", replay_offset_, replay_end_);
    maybeReadNextChunk();
  }
}

void BufferManager::maybeDrainInjected() {
  ASSERT(!destroyed_);
  if (!replaying_) {
    return;
  }

  while (inject_data_.length() > 0) {
    if (bridge_.replayPaused()) {
      ENVOY_LOG(trace, "ai_protocol_manager: inject paused (chain back-pressure)");
      return;
    }
    if (!bridge_.tryConsumeInjectBudget()) {
      ENVOY_LOG(trace, "ai_protocol_manager: inject yielding (budget spent)");
      budget_yielded_ = true;
      replayCallback().scheduleCallbackNextIteration();
      return;
    }
    const uint64_t to_inject =
        std::min(ReadChunkSize, static_cast<uint64_t>(inject_data_.length()));
    Buffer::OwnedImpl chunk;
    chunk.move(inject_data_, to_inject);
    bridge_.injectData(chunk);
    if (destroyed_ || !replaying_) {
      return;
    }
  }

  finishReplay();
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
  // Pause while the chain we feed is backed up; onReplayResumed() restarts us
  // once it drains. This bounds how much replayed data piles up in the chain when
  // it is slow.
  if (bridge_.replayPaused()) {
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

  // The range became readable before the replay started, and endStream() rules out the ingest
  // that could have taken it away again.
  ASSERT(allBytesReadable());

  // Bound the work one event-loop pass can do. A store that completes the read
  // on-stack re-enters here (via onReadComplete) from within the buffer_->read()
  // call below and would otherwise replay the whole payload back-to-back; the
  // budget stops that and caps the recursion depth with it. An async store paces
  // itself one chunk per completion, each of which refills the budget, so it never
  // reaches the cap.
  if (!bridge_.tryConsumeInjectBudget()) {
    ENVOY_LOG(trace, "ai_protocol_manager: replay yielding at offset {} (inject budget spent)",
              replay_offset_);
    budget_yielded_ = true;
    replayCallback().scheduleCallbackNextIteration();
    return;
  }

  const uint64_t chunk = std::min(ReadChunkSize, replay_end_ - replay_offset_);
  read_in_flight_ = true;
  in_read_ = true;
  if (buffer_ == nullptr) {
    // In-memory tier: pending_ holds the whole payload from offset 0, so serve the chunk directly.
    // Completing on-stack keeps this on the synchronous-store path, which the inject budget
    // already bounds.
    auto data = std::make_unique<Buffer::OwnedImpl>();
    auto bytes = std::make_unique<uint8_t[]>(chunk);
    pending_.copyOut(replay_offset_, chunk, bytes.get());
    data->add(bytes.get(), chunk);
    onReadComplete(ExternalBufferStatus::Ok, std::move(data));
  } else {
    buffer_->read(replay_offset_, chunk,
                  [this](ExternalBufferStatus status, Buffer::InstancePtr data) {
                    onReadComplete(status, std::move(data));
                  });
  }
  // A synchronous store completes this read on-stack (onReadComplete, and on the
  // final chunk the replay-done callback) before read() returns, which may detach us
  // via onDestroy(). Because the caller at the root of this call stack
  // (onReplayContinuation or onWriteComplete) pins `self`, `*this` remains alive
  // until read() and maybeReadNextChunk() return.
  in_read_ = false;
}

void BufferManager::onReplayContinuation() {
  // Asynchronous entry point (via Event::SchedulableCallback capturing raw `this`).
  // Pinning `self` here protects all downstream helper calls (maybeStartReplay,
  // maybeDrainInjected, maybeReadNextChunk, finishReplay) for the entire stack frame.
  auto self = weak_from_this().lock();
  // onDestroy() cancels replay_cb_, so the continuation never fires once detached.
  ASSERT(!destroyed_);
  if (replay_cancelled_) {
    return;
  }
  if (budget_yielded_) {
    // This continuation was scheduled for the next event-loop pass, so the budget it yielded on
    // has expired with the pass that spent it.
    budget_yielded_ = false;
    bridge_.resetInjectBudget();
  }
  // Off the caller's stack: either start replay deferred from replay() (the
  // offload was already durable when the caller requested it), resume after the
  // inject budget was spent, or resume after chain back-pressure drained (deferred
  // out of the watermark callback). maybeStartReplay() already drives the first
  // read, so dispatch on whether replay is underway to avoid advancing twice.
  if (!replaying_) {
    maybeStartReplay();
  } else if (replay_source_ == ReplaySource::Injected) {
    maybeDrainInjected();
  } else if (replay_source_ == ReplaySource::ExternalBuffer) {
    maybeReadNextChunk();
  }
}

void BufferManager::onReadComplete(ExternalBufferStatus status, Buffer::InstancePtr data) {
  // Asynchronous entry point (via ExternalBuffer::read callback capturing raw `this`).
  // Pinning `self` here protects all downstream calls (injectData, finishReplay,
  // maybeReadNextChunk, onExternalBufferError) for the entire stack frame.
  auto self = weak_from_this().lock();
  // A conforming store cancels pending completions on destruction, so we never enter
  // here already detached. (destroyed_ can still flip mid-method when the inject
  // below ends the stream -- handled by the runtime check after injectData().)
  ASSERT(!destroyed_);
  read_in_flight_ = false;
  if (!in_read_) {
    // An asynchronous store returns its completion through the dispatcher, so this is a later pass
    // than the one that issued the read.
    bridge_.resetInjectBudget();
  }
  if (replay_cancelled_ || !replaying_) {
    return;
  }
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
  bridge_.injectData(*data);
  if (destroyed_ || !replaying_) {
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
  replay_source_ = ReplaySource::None;
  bridge_.clearReplayHandler(*this);
  ENVOY_LOG(trace, "ai_protocol_manager: replay complete");
  // Hand control back to the caller. Its callback may start the next sub-range or
  // terminate the stream; move the callback out first so it can re-arm replay().
  ReplayDoneCallback done = std::move(replay_done_);
  replay_done_ = nullptr;
  if (done) {
    done(absl::OkStatus());
  }
}

void BufferManager::onExternalBufferError() {
  ENVOY_LOG(warn, "ai_protocol_manager: external buffer I/O error, failing stream");
  bridge_.onUnrecoverableError();
}

void BufferManager::onReplayResumed() {
  if (budget_yielded_) {
    // Already scheduled for next iteration to refill the inject budget; do not overwrite with
    // scheduleCallbackCurrentIteration().
    return;
  }
  // Resume replay where we paused, deferred via replay_cb_ rather than reading here: this runs
  // inside an Envoy watermark callback, and injecting a chunk (or failing the stream on a buffer
  // error) can re-enter the watermark callbacks during router cleanup, which is unsafe. The
  // continuation runs once this callback unwinds. If a replay_cb_ was already scheduled (e.g. a
  // per-iteration yield), this is idempotent.
  replayCallback().scheduleCallbackCurrentIteration();
}

} // namespace AiProtocolManager
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
