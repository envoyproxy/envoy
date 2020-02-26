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
  void decode100ContinueHeaders(ResponseHeaderMapPtr&& headers) override {
    inner_.decode100ContinueHeaders(std::move(headers));
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
  void encodeHeaders(const RequestHeaderMap& headers, bool end_stream) override {
    inner_.encodeHeaders(headers, end_stream);
    if (end_stream) {
      onEncodeComplete();
    }
  }

  void encodeData(Buffer::Instance& data, bool end_stream) override {
    inner_.encodeData(data, end_stream);
    if (end_stream) {
      onEncodeComplete();
    }
  }

  void encodeTrailers(const RequestTrailerMap& trailers) override {
    inner_.encodeTrailers(trailers);
    onEncodeComplete();
  }

  void encodeMetadata(const MetadataMapVector& metadata_map_vector) override {
    inner_.encodeMetadata(metadata_map_vector);
  }

  Stream& getStream() override { return inner_.getStream(); }

protected:
  RequestEncoderWrapper(RequestEncoder& inner) : inner_(inner) {}

  /**
   * Consumers of the wrapper generally want to know when an encode is complete. This is called at
   * that time and is implemented by derived classes.
   */
  virtual void onEncodeComplete() PURE;

  RequestEncoder& inner_;
};

} // namespace Http
} // namespace Envoy
