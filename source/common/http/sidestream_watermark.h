#pragma once

#include "envoy/http/async_client.h"

#include "source/common/runtime/runtime_features.h"

namespace Envoy {
namespace Http {

/**
 * Sidestream watermark callback implementation for stream filter that either handles decoding only
 * or handles both encoding and decoding.
 */
class StreamFilterSidestreamWatermarkCallbacks : public Http::SidestreamWatermarkCallbacks {
public:
  void onSidestreamAboveHighWatermark() final {
    // Sidestream push back downstream, if callback is configured.
    if (decode_callback_ != nullptr) {
      decode_callback_->onDecoderFilterAboveWriteBufferHighWatermark();
    }

    // Sidestream push back upstream, if callback is configured.
    if (encode_callback_ != nullptr) {
      encode_callback_->onEncoderFilterAboveWriteBufferHighWatermark();
    }
  }

  void onSidestreamBelowLowWatermark() final {
    if (decode_callback_ != nullptr) {
      decode_callback_->onDecoderFilterBelowWriteBufferLowWatermark();
    }

    if (encode_callback_ != nullptr) {
      encode_callback_->onEncoderFilterBelowWriteBufferLowWatermark();
    }
  }

  void addDownstreamWatermarkCallbacks(Http::DownstreamWatermarkCallbacks& callbacks) final {
    if (Runtime::runtimeFeatureEnabled(
            "envoy.reloadable_features.grpc_side_stream_flow_control_3")) {
      downstream_watermark_callbacks_ = &callbacks;
      if (downstream_pushback_enabled_ && decode_callback_ != nullptr) {
        decode_callback_->addSidestreamWatermarkCallbacks(callbacks);
      }
    }
  }

  void removeDownstreamWatermarkCallbacks(Http::DownstreamWatermarkCallbacks& callbacks) final {
    if (downstream_pushback_enabled_ && downstream_watermark_callbacks_ != nullptr &&
        decode_callback_ != nullptr) {
      decode_callback_->removeSidestreamWatermarkCallbacks(callbacks);
    }
    downstream_watermark_callbacks_ = nullptr;
  }

  /**
   * Enable or disable downstream watermark backpressure to the side-stream. When enabled and a
   * side-stream callback is present, subscribes to downstream connection/stream watermark events
   * via addSidestreamWatermarkCallbacks (which immediately replays any active downstream high
   * watermarks). When disabled, unsubscribes from downstream watermark events.
   */
  void setDownstreamWatermarkPushbackEnabled(bool enabled) {
    if (!Runtime::runtimeFeatureEnabled(
            "envoy.reloadable_features.grpc_side_stream_flow_control_3") ||
        decode_callback_ == nullptr || downstream_pushback_enabled_ == enabled) {
      return;
    }
    downstream_pushback_enabled_ = enabled;
    if (downstream_watermark_callbacks_ == nullptr) {
      return;
    }
    if (downstream_pushback_enabled_) {
      decode_callback_->addSidestreamWatermarkCallbacks(*downstream_watermark_callbacks_);
    } else {
      decode_callback_->removeSidestreamWatermarkCallbacks(*downstream_watermark_callbacks_);
    }
  }

  bool downstreamWatermarkPushbackEnabled() const { return downstream_pushback_enabled_; }

  /**
   * The set function needs to be called by stream decoder filter before side stream connection is
   * established, to apply the backpressure to downstream when it is above watermark,
   */
  void setDecoderFilterCallbacks(Http::StreamDecoderFilterCallbacks* decode_callback) {
    decode_callback_ = decode_callback;
  }

  /**
   * The set function needs to be called by stream encoder filter before side stream connection is
   * established, to apply the backpressure to upstream when it is above watermark,
   */
  void setEncoderFilterCallbacks(Http::StreamEncoderFilterCallbacks* encode_callback) {
    encode_callback_ = encode_callback;
  }

private:
  // Non owning pointers; `removeWatermarkCallbacks()` needs to be called to unregister watermark
  // callbacks (if any) before filter callbacks are destroyed. Typically when stream is being closed
  // or filter is being destroyed.
  Http::StreamDecoderFilterCallbacks* decode_callback_ = nullptr;
  Http::StreamEncoderFilterCallbacks* encode_callback_ = nullptr;
  Http::DownstreamWatermarkCallbacks* downstream_watermark_callbacks_ = nullptr;
  bool downstream_pushback_enabled_ = false;
};

} // namespace Http
} // namespace Envoy
