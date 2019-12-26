#pragma once
#include "envoy/http/codec.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Http {

class MockStreamDecoder : public StreamDecoder {
public:
  MockStreamDecoder();
  ~MockStreamDecoder() override;

  void decode100ContinueHeaders(HeaderMapPtr&& headers) override {
    decode100ContinueHeaders_(headers);
  }
  void decodeHeaders(HeaderMapPtr&& headers, bool end_stream) override {
    decodeHeaders_(headers, end_stream);
  }
  void decodeTrailers(HeaderMapPtr&& trailers) override { decodeTrailers_(trailers); }

  void decodeMetadata(MetadataMapPtr&& metadata_map) override { decodeMetadata_(metadata_map); }

  // Http::StreamDecoder
  MOCK_METHOD2(decodeHeaders_, void(HeaderMapPtr& headers, bool end_stream));
  MOCK_METHOD1(decode100ContinueHeaders_, void(HeaderMapPtr& headers));
  MOCK_METHOD2(decodeData, void(Buffer::Instance& data, bool end_stream));
  MOCK_METHOD1(decodeTrailers_, void(HeaderMapPtr& trailers));
  MOCK_METHOD1(decodeMetadata_, void(MetadataMapPtr& metadata_map));
};

} // namespace Http
} // namespace Envoy
