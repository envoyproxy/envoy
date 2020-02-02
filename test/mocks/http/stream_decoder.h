#pragma once
#include "envoy/http/codec.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Http {

class MockStreamDecoder : public virtual StreamDecoder {
public:
  MockStreamDecoder();
  ~MockStreamDecoder() override;

  void decodeMetadata(MetadataMapPtr&& metadata_map) override { decodeMetadata_(metadata_map); }

  // Http::StreamDecoder
  MOCK_METHOD(void, decodeData, (Buffer::Instance & data, bool end_stream));
  MOCK_METHOD(void, decodeMetadata_, (MetadataMapPtr & metadata_map));
};

class MockRequestDecoder : public MockStreamDecoder, public RequestDecoder {
public:
  MockRequestDecoder();
  ~MockRequestDecoder();

  void decodeHeaders(HeaderMapPtr&& headers, bool end_stream) override {
    decodeHeaders_(headers, end_stream);
  }
  void decodeTrailers(HeaderMapPtr&& trailers) override { decodeTrailers_(trailers); }

  // Http::RequestDecoder
  MOCK_METHOD(void, decodeHeaders_, (HeaderMapPtr & headers, bool end_stream));
  MOCK_METHOD(void, decodeTrailers_, (HeaderMapPtr & trailers));
};

class MockResponseDecoder : public MockStreamDecoder, public ResponseDecoder {
public:
  MockResponseDecoder();
  ~MockResponseDecoder();

  void decode100ContinueHeaders(HeaderMapPtr&& headers) override {
    decode100ContinueHeaders_(headers);
  }
  void decodeHeaders(HeaderMapPtr&& headers, bool end_stream) override {
    decodeHeaders_(headers, end_stream);
  }
  void decodeTrailers(HeaderMapPtr&& trailers) override { decodeTrailers_(trailers); }

  // Http::ResponseDecoder
  MOCK_METHOD(void, decode100ContinueHeaders_, (HeaderMapPtr & headers));
  MOCK_METHOD(void, decodeHeaders_, (HeaderMapPtr & headers, bool end_stream));
  MOCK_METHOD(void, decodeTrailers_, (HeaderMapPtr & trailers));
};

} // namespace Http
} // namespace Envoy
