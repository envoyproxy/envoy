#pragma once

#include "envoy/http/async_client.h"

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
};

} // namespace Http
} // namespace Envoy
