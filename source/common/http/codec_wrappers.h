#pragma once

#include "envoy/http/codec.h"

#include "source/common/http/response_decoder_impl_base.h"
#include "source/common/runtime/runtime_features.h"

namespace Envoy {
namespace Http {

/**
 * Wrapper for ResponseDecoder that just forwards to an "inner" decoder.
 */
class ResponseDecoderWrapper : public ResponseDecoderImplBase {
public:
  // ResponseDecoder
  void decode1xxHeaders(ResponseHeaderMapPtr&& headers) override {
    if (Http::ResponseDecoder* inner = getInnerDecoder()) {
      inner->decode1xxHeaders(std::move(headers));
    } else {
      onInnerDecoderDead();
    }
  }

  void decodeHeaders(ResponseHeaderMapPtr&& headers, bool end_stream) override {
    if (end_stream) {
      onPreDecodeComplete();
    }
    if (Http::ResponseDecoder* inner = getInnerDecoder()) {
      inner->decodeHeaders(std::move(headers), end_stream);
    } else {
      onInnerDecoderDead();
    }
    if (end_stream) {
      onDecodeComplete();
    }
  }

  void decodeData(Buffer::Instance& data, bool end_stream) override {
    if (end_stream) {
      onPreDecodeComplete();
    }
    if (Http::ResponseDecoder* inner = getInnerDecoder()) {
      inner->decodeData(data, end_stream);
    } else {
      onInnerDecoderDead();
    }
    if (end_stream) {
      onDecodeComplete();
    }
  }

  void decodeTrailers(ResponseTrailerMapPtr&& trailers) override {
    onPreDecodeComplete();
    if (Http::ResponseDecoder* inner = getInnerDecoder()) {
      inner->decodeTrailers(std::move(trailers));
    } else {
      onInnerDecoderDead();
    }
    onDecodeComplete();
  }

  void decodeMetadata(MetadataMapPtr&& metadata_map) override {
    if (Http::ResponseDecoder* inner = getInnerDecoder()) {
      inner->decodeMetadata(std::move(metadata_map));
    } else {
      onInnerDecoderDead();
    }
  }

  void dumpState(std::ostream& os, int indent_level) const override {
    if (Http::ResponseDecoder* inner = getInnerDecoder()) {
      inner->dumpState(os, indent_level);
    } else {
      onInnerDecoderDead();
    }
  }

protected:
  ResponseDecoderWrapper(ResponseDecoder& inner) : inner_(&inner) {}

  /**
   * @param inner_handle refers a response decoder which may have already died at
   * this point. Following access to the decoder will check its liveliness.
   */
  ResponseDecoderWrapper(ResponseDecoderHandlePtr inner_handle)
      : inner_handle_(std::move(inner_handle)) {}

  /**
   * Consumers of the wrapper generally want to know when a decode is complete. This is called
   * at that time and is implemented by derived classes.
   */
  virtual void onPreDecodeComplete() PURE;
  virtual void onDecodeComplete() PURE;

  ResponseDecoderHandlePtr inner_handle_;
  Http::ResponseDecoder* inner_ = nullptr;

private:
  Http::ResponseDecoder* getInnerDecoder() const {
    if (inner_handle_ == nullptr) {
      return inner_;
    }
    if (inner_handle_) {
      if (OptRef<ResponseDecoder> inner = inner_handle_->get(); inner.has_value()) {
        return &inner.value().get();
      }
    }
    return nullptr;
  }

  void onInnerDecoderDead() const {
    const std::string error_msg = "Wrapped decoder use after free detected.";
    IS_ENVOY_BUG(error_msg);
    RELEASE_ASSERT(!Runtime::runtimeFeatureEnabled(
                       "envoy.reloadable_features.abort_when_accessing_dead_decoder"),
                   error_msg);
  }
};

/**
 * Wrapper for RequestEncoder that just forwards to an "inner" encoder.
 */
class RequestEncoderWrapper : public RequestEncoder {
public:
  // RequestEncoder
  Status encodeHeaders(const RequestHeaderMap& headers, bool end_stream) override {
    ASSERT(inner_encoder_);
    RETURN_IF_ERROR(inner_encoder_->encodeHeaders(headers, end_stream));
    if (end_stream) {
      onEncodeComplete();
    }
    return okStatus();
  }

  void encodeData(Buffer::Instance& data, bool end_stream) override {
    ASSERT(inner_encoder_);
    inner_encoder_->encodeData(data, end_stream);
    if (end_stream) {
      onEncodeComplete();
    }
  }

  void encodeTrailers(const RequestTrailerMap& trailers) override {
    ASSERT(inner_encoder_);
    inner_encoder_->encodeTrailers(trailers);
    onEncodeComplete();
  }

  void enableTcpTunneling() override {
    ASSERT(inner_encoder_);
    inner_encoder_->enableTcpTunneling();
  }

  void encodeMetadata(const MetadataMapVector& metadata_map_vector) override {
    ASSERT(inner_encoder_);
    inner_encoder_->encodeMetadata(metadata_map_vector);
  }

  Stream& getStream() override {
    ASSERT(inner_encoder_);
    return inner_encoder_->getStream();
  }

  Http1StreamEncoderOptionsOptRef http1StreamEncoderOptions() override {
    ASSERT(inner_encoder_);
    return inner_encoder_->http1StreamEncoderOptions();
  }

protected:
  RequestEncoderWrapper(RequestEncoder* inner) : inner_encoder_(inner) {}

  /**
   * Consumers of the wrapper generally want to know when an encode is complete. This is called at
   * that time and is implemented by derived classes.
   */
  virtual void onEncodeComplete() PURE;

  RequestEncoder* inner_encoder_;
};

} // namespace Http
} // namespace Envoy
