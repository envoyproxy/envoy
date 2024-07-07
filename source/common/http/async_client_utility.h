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
    if (decode_callback_ != nullptr) {
      decode_callback_->onDecoderFilterAboveWriteBufferHighWatermark();
    } else {
      // TODO(tyxia) Log
    }

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

// TODO(tyxia) Remove, This class is merged into class above, which can be either decoder filter
// only or dual filter.
// /**
//  * Sidestream watermark callback implementation for stream decoder filter that handles decoding.
//  */
// class StreamDecoderFilterSidestreamWatermarkCallbacks : public Http::SidestreamWatermarkCallbacks
// { public:
//   StreamDecoderFilterSidestreamWatermarkCallbacks() = default;

//   void onAboveWriteBufferHighWatermark() final {
//     if (callback_ != nullptr) {
//       callback_->onDecoderFilterAboveWriteBufferHighWatermark();
//     } else {
//     }
//   }

//   void onBelowWriteBufferLowWatermark() final {
//     if (callback_ != nullptr) {
//       callback_->onDecoderFilterBelowWriteBufferLowWatermark();
//     } else {
//     }
//   }

//   void setDecoderFilterCallbacks(Http::StreamDecoderFilterCallbacks* callback) {
//     callback_ = callback;
//   }

//   void resetFilterCallbacks() { callback_ = nullptr; }

// private:
//   Http::StreamDecoderFilterCallbacks* callback_ = nullptr;
// };

} // namespace Http
} // namespace Envoy
