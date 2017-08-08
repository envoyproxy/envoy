#pragma once

#include "envoy/http/codec.h"

namespace Envoy {
namespace Http {

/**
 * Wrapper for StreamDecoder that just forwards to an "inner" decoder.
 */
class StreamDecoderWrapper : public StreamDecoder {
public:
  // StreamDecoder
  void decodeHeaders(HeaderMapPtr&& headers, bool end_stream) override {
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

  void decodeTrailers(HeaderMapPtr&& trailers) override {
    onPreDecodeComplete();
    inner_.decodeTrailers(std::move(trailers));
    onDecodeComplete();
  }

protected:
  StreamDecoderWrapper(StreamDecoder& inner) : inner_(inner) {}

  /**
   * Consumers of the wrapper generally want to know when a decode is complete. This is called
   * at that time and is implemented by derived classes.
   */
  virtual void onPreDecodeComplete() PURE;
  virtual void onDecodeComplete() PURE;

  StreamDecoder& inner_;
};

/**
 * Wrapper for StreamEncoder that just forwards to an "inner" encoder.
 */
class StreamEncoderWrapper : public StreamEncoder {
public:
  // StreamEncoder
  void encodeHeaders(const HeaderMap& headers, bool end_stream) override {
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

  void encodeTrailers(const HeaderMap& trailers) override {
    inner_.encodeTrailers(trailers);
    onEncodeComplete();
  }

  Stream& getStream() override { return inner_.getStream(); }

protected:
  StreamEncoderWrapper(StreamEncoder& inner) : inner_(inner) {}

  /**
   * Consumers of the wrapper generally want to know when an encode is complete. This is called at
   * that time and is implemented by derived classes.
   */
  virtual void onEncodeComplete() PURE;

  StreamEncoder& inner_;
};

} // namespace Http
} // namespace Envoy
