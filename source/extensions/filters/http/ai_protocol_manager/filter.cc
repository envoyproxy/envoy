#include "source/extensions/filters/http/ai_protocol_manager/filter.h"

#include <algorithm>
#include <utility>

#include "envoy/http/codes.h"

#include "source/common/buffer/buffer_impl.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AiProtocolManager {

void AiProtocolManagerFilter::setDecoderFilterCallbacks(
    Http::StreamDecoderFilterCallbacks& callbacks) {
  PassThroughFilter::setDecoderFilterCallbacks(callbacks);
  // Subscribe to upstream-request watermarks so replay can be paced against
  // upstream back-pressure. Note: subscribing may immediately deliver
  // high-watermark callbacks if the upstream is already backed up.
  decoder_callbacks_->addUpstreamWatermarkCallbacks(*this);
  watermark_callbacks_registered_ = true;
}

void AiProtocolManagerFilter::onDestroy() {
  destroyed_ = true;
  if (watermark_callbacks_registered_) {
    decoder_callbacks_->removeUpstreamWatermarkCallbacks(*this);
    watermark_callbacks_registered_ = false;
  }
  // Dropping the buffer cancels any pending append/read completion callbacks.
  buffer_.reset();
}

Http::FilterDataStatus AiProtocolManagerFilter::decodeData(Buffer::Instance& data,
                                                           bool end_stream) {
  if (buffer_ == nullptr) {
    buffer_ = buffer_factory_.createBuffer(decoder_callbacks_->dispatcher());
    // Apply backpressure based on the configured decoder buffer limit so the
    // amount of in-flight (not-yet-durable) payload stays bounded. Resume once
    // it has drained to half the limit.
    const uint32_t high = decoder_callbacks_->decoderBufferLimit();
    buffer_->setWatermarks(high, high / 2, *this);
  }

  end_stream_seen_ = end_stream;
  ++outstanding_appends_;
  ENVOY_LOG(trace, "ai_protocol_manager: offloading {} bytes (end_stream={})", data.length(),
            end_stream);
  buffer_->append(data, [this](ExternalBufferStatus status) { onAppendComplete(status); });

  // We own all buffering and continuation: hold the chain here and replay the
  // payload ourselves once it has been fully offloaded.
  return Http::FilterDataStatus::StopIterationNoBuffer;
}

void AiProtocolManagerFilter::onAppendComplete(ExternalBufferStatus status) {
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

void AiProtocolManagerFilter::streamBackToFilterChain() {
  replaying_ = true;
  replay_offset_ = 0;
  replay_length_ = buffer_->length();
  ENVOY_LOG(debug, "ai_protocol_manager: replaying {} buffered bytes", replay_length_);
  maybeReadNextChunk();
}

void AiProtocolManagerFilter::maybeReadNextChunk() {
  if (!replaying_ || read_in_flight_) {
    return;
  }
  // Pause while the chain we feed is backed up; onBelowWriteBufferLowWatermark()
  // resumes us once it drains. This is what bounds how much replayed data piles
  // up downstream when the upstream is slow.
  if (upstream_high_watermark_count_ > 0) {
    ENVOY_LOG(trace, "ai_protocol_manager: replay paused at offset {} (upstream back-pressure)",
              replay_offset_);
    return;
  }

  if (replay_offset_ >= replay_length_) {
    // Empty payload (or an empty trailing frame): emit an end_stream marker so
    // downstream filters see stream completion.
    replaying_ = false;
    Buffer::OwnedImpl empty;
    decoder_callbacks_->injectDecodedDataToFilterChain(empty, true);
    return;
  }

  const uint64_t chunk = std::min(ReadChunkSize, replay_length_ - replay_offset_);
  read_in_flight_ = true;
  buffer_->read(replay_offset_, chunk,
                [this](ExternalBufferStatus status, Buffer::InstancePtr data) {
                  onReadComplete(status, std::move(data));
                });
}

void AiProtocolManagerFilter::onReadComplete(ExternalBufferStatus status,
                                             Buffer::InstancePtr data) {
  if (destroyed_) {
    return;
  }
  read_in_flight_ = false;
  if (status != ExternalBufferStatus::Ok) {
    onExternalBufferError();
    return;
  }

  replay_offset_ += data->length();
  const bool end_stream = replay_offset_ >= replay_length_;
  // Inject even if a high watermark was raised while this read was in flight:
  // at most one extra chunk (ReadChunkSize) overshoots before we pause, which
  // keeps the overshoot bounded.
  decoder_callbacks_->injectDecodedDataToFilterChain(*data, end_stream);
  if (end_stream) {
    replaying_ = false;
    return;
  }
  maybeReadNextChunk();
}

void AiProtocolManagerFilter::onExternalBufferError() {
  ENVOY_LOG(warn, "ai_protocol_manager: external buffer I/O error, failing request");
  decoder_callbacks_->sendLocalReply(Http::Code::InternalServerError, "AI protocol buffer error",
                                     nullptr, std::nullopt,
                                     "ai_protocol_manager_external_buffer_error");
}

void AiProtocolManagerFilter::onAboveHighWatermark() {
  decoder_callbacks_->onDecoderFilterAboveWriteBufferHighWatermark();
}

void AiProtocolManagerFilter::onBelowLowWatermark() {
  decoder_callbacks_->onDecoderFilterBelowWriteBufferLowWatermark();
}

void AiProtocolManagerFilter::onAboveWriteBufferHighWatermark() {
  // May be called multiple times (stream and connection); count so we resume
  // only after a matching number of low-watermark callbacks.
  ++upstream_high_watermark_count_;
  ENVOY_LOG(debug, "ai_protocol_manager: upstream high watermark (depth={})",
            upstream_high_watermark_count_);
}

void AiProtocolManagerFilter::onBelowWriteBufferLowWatermark() {
  ASSERT(upstream_high_watermark_count_ > 0);
  --upstream_high_watermark_count_;
  ENVOY_LOG(debug, "ai_protocol_manager: upstream low watermark (depth={})",
            upstream_high_watermark_count_);
  if (upstream_high_watermark_count_ == 0) {
    // Drained: resume replay where we paused.
    maybeReadNextChunk();
  }
}

} // namespace AiProtocolManager
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
