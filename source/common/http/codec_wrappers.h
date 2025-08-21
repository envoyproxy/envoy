#pragma once

#include "envoy/http/codec.h"

namespace Envoy {
namespace Http {

/**
 * Wrapper for ResponseDecoder that just forwards to an "inner" decoder.
 */
class ResponseDecoderWrapper : public ResponseDecoder {
public:
  // ResponseDecoder
  void decode1xxHeaders(ResponseHeaderMapPtr&& headers) override {
    inner_.decode1xxHeaders(std::move(headers));
  }

  void decodeHeaders(ResponseHeaderMapPtr&& headers, bool end_stream) override {
    if (end_stream) {
      onPreDecodeComplete();
    }
    inner_.decodeHeaders(std::move(headers), end_stream);
    if (end_stream) {
      onDecodeComplete();
    }
  }

  void decodeData(Buffer::Instance& data, bool end_stream) override {
    if (end_stream) {
      onPreDecodeComplete();
    }
    inner_.decodeData(data, end_stream);
    if (end_stream) {
      onDecodeComplete();
    }
  }

  void decodeTrailers(ResponseTrailerMapPtr&& trailers) override {
    onPreDecodeComplete();
    inner_.decodeTrailers(std::move(trailers));
    onDecodeComplete();
  }

  void decodeMetadata(MetadataMapPtr&& metadata_map) override {
    inner_.decodeMetadata(std::move(metadata_map));
  }

  void dumpState(std::ostream& os, int indent_level) const override {
    inner_.dumpState(os, indent_level);
  }

protected:
  ResponseDecoderWrapper(ResponseDecoder& inner) : inner_(inner) {}

  /**
   * Consumers of the wrapper generally want to know when a decode is complete. This is called
   * at that time and is implemented by derived classes.
   */
  virtual void onPreDecodeComplete() PURE;
  virtual void onDecodeComplete() PURE;

  ResponseDecoder& inner_;
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
