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
  // Subscribe to the path's replay watermarks so replay can be paced against
  // chain back-pressure. Note: subscribing may immediately deliver
  // high-watermark callbacks if the chain is already backed up; those only bump
  // the pause counter, which is safe here.
  bridge_->registerReplayWatermarks(*this);
}

void BufferManager::onDestroy() {
  destroyed_ = true;
  bridge_->unregisterReplayWatermarks();
  // Dropping the buffer cancels any pending append/read completion callbacks.
  buffer_.reset();
}

Http::FilterDataStatus BufferManager::onData(Buffer::Instance& data, bool end_stream) {
  if (buffer_ == nullptr) {
    buffer_ = buffer_factory_.createBuffer(bridge_->dispatcher());
    // Apply backpressure based on the configured buffer limit so the amount of
    // in-flight (not-yet-durable) payload stays bounded. Resume once it has
    // drained to half the limit.
    const uint32_t high = bridge_->bufferLimit();
    buffer_->setWatermarks(high, high / 2, *this);
  }

  end_stream_seen_ = end_stream;
  ++outstanding_appends_;
  ENVOY_LOG(trace, "ai_protocol_manager: offloading {} bytes (end_stream={})", data.length(),
            end_stream);
  // Take ownership of the bytes before append() returns, so the filter chain's
  // buffer reference does not dangle across the asynchronous offload. This
  // hand-off is storage-agnostic, so it lives here once rather than being
  // repeated by every ExternalBuffer implementation.
  auto owned = std::make_unique<Buffer::OwnedImpl>();
  owned->move(data);
  buffer_->append(std::move(owned),
                  [this](ExternalBufferStatus status) { onAppendComplete(status); });

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
  // the trailers. If appends are still outstanding, onAppendComplete() starts
  // replay once they drain.
  trailers_pending_ = true;
  end_stream_seen_ = true;
  ENVOY_LOG(trace, "ai_protocol_manager: trailers observed; stream complete");
  if (outstanding_appends_ == 0) {
    streamBackToFilterChain();
  }

  // Hold the trailers behind the replayed body; finishReplay() releases them via
  // bridge_->continueIteration() once the last body frame has been injected.
  return Http::FilterTrailersStatus::StopIteration;
}

void BufferManager::onAppendComplete(ExternalBufferStatus status) {
  if (destroyed_) {
    return;
  }
  if (status != ExternalBufferStatus::Ok) {
    onExternalBufferError();
    return;
  }

  ASSERT(outstanding_appends_ > 0);
  --outstanding_appends_;

  // Begin replay only after the last byte has been offloaded.
  if (end_stream_seen_ && outstanding_appends_ == 0) {
    streamBackToFilterChain();
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
  // resumes us once it drains. This is what bounds how much replayed data piles
  // up downstream when the chain is slow.
  if (replay_high_watermark_count_ > 0) {
    ENVOY_LOG(trace, "ai_protocol_manager: replay paused at offset {} (chain back-pressure)",
              replay_offset_);
    return;
  }

  if (replay_offset_ >= replay_length_) {
    // Empty payload (replay_length_ == 0): there is nothing to read. Emit an
    // empty end_stream marker so downstream filters see completion -- unless
    // trailers will carry end_stream, in which case finishReplay() releases them.
    if (!trailers_pending_) {
      Buffer::OwnedImpl empty;
      bridge_->injectData(empty, true);
    }
    finishReplay();
    return;
  }

  const uint64_t chunk = std::min(ReadChunkSize, replay_length_ - replay_offset_);
  read_in_flight_ = true;
  buffer_->read(replay_offset_, chunk,
                [this](ExternalBufferStatus status, Buffer::InstancePtr data) {
                  onReadComplete(status, std::move(data));
                });
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

void BufferManager::onAboveHighWatermark() { bridge_->pauseSource(); }

void BufferManager::onBelowLowWatermark() { bridge_->resumeSource(); }

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
