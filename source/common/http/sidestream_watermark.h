#pragma once

#include "envoy/http/filter.h"

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
    } else {
      // TODO(tyxia) Log
    }

    // Sidestream push back upstream, if callback is configured.
    if (encode_callback_ != nullptr) {
      encode_callback_->onEncoderFilterAboveWriteBufferHighWatermark();
    } else {
      // TODO(tyxia) Log
    }
  }

  void onSidestreamBelowLowWatermark() final {
    if (decode_callback_ != nullptr) {
      decode_callback_->onDecoderFilterBelowWriteBufferLowWatermark();
    }

    if (encode_callback_ != nullptr) {
      encode_callback_->onEncoderFilterBelowWriteBufferLowWatermark();
    } else {
    }
  }

  void setDecoderFilterCallbacks(Http::StreamDecoderFilterCallbacks* decode_callback) {
    decode_callback_ = decode_callback;
  }

  void setEncoderFilterCallbacks(Http::StreamEncoderFilterCallbacks* encode_callback) {
    encode_callback_ = encode_callback;
  }

  void resetFilterCallbacks() {
    decode_callback_ = nullptr;
    encode_callback_ = nullptr;
  }

private:
  Http::StreamDecoderFilterCallbacks* decode_callback_ = nullptr;
  Http::StreamEncoderFilterCallbacks* encode_callback_ = nullptr;
};

} // namespace Http
} // namespace Envoy
