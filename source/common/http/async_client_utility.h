#pragma once

#include "envoy/http/async_client.h"

namespace Envoy {
namespace Http {

/**
 * Keeps track of active async HTTP requests to be able to cancel them on destruction.
 */
class AsyncClientRequestTracker {
public:
  /**
   * Cancels all known active async HTTP requests.
   */
  ~AsyncClientRequestTracker();
  /**
   * Includes a given async HTTP request into a set of known active requests.
   * @param request request handle
   */
  void add(AsyncClient::Request& request);
  /**
   * Excludes a given async HTTP request from a set of known active requests.
   *
   * NOTE: Asymmetry between signatures of add() and remove() is caused by the difference
   *       between contexts in which these methods will be used.
   *       add() will be called right after AsyncClient::send() when request.cancel() is
   *       perfectly valid and desirable.
   *       However, remove() will be called in the context of
   *       AsyncClient::Callbacks::[onSuccess | onFailure] where request.cancel() is no longer
   *       expected and therefore get prevented by means of "const" modifier.
   *
   * @param request request handle
   */
  void remove(const AsyncClient::Request& request);

private:
  // Track active async HTTP requests to be able to cancel them on destruction.
  absl::flat_hash_set<AsyncClient::Request*> active_requests_;
};

/**
 * Sidestream watermark callback implementation for stream filter that either handles decoding only
 * or handles both encoding and decoding.
 */
class StreamFilterSidestreamWatermarkCallbacks : public Http::SidestreamWatermarkCallbacks {
public:
  StreamFilterSidestreamWatermarkCallbacks() = default;

  void onAboveWriteBufferHighWatermark() final {
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

  void onBelowWriteBufferLowWatermark() final {
    if (decode_callback_ != nullptr) {
      decode_callback_->onDecoderFilterBelowWriteBufferLowWatermark();
    }

    if (encode_callback_ != nullptr) {
      encode_callback_->onEncoderFilterBelowWriteBufferLowWatermark();
    } else {
    }
  }

  void addDownstreamWatermarkCallbacks(Http::DownstreamWatermarkCallbacks& callbacks) final {
    // Sidestream subscribes to downstream watermark events.
    decoder_callbacks_->addDownstreamWatermarkCallbacks(callbacks);
  }

  void removeDownstreamWatermarkCallbacks(Http::DownstreamWatermarkCallbacks& callbacks) final {
    decoder_callbacks_->removeDownstreamWatermarkCallbacks(callbacks);
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
