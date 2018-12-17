#pragma once

#include "envoy/http/codec.h"

#include "test/mocks/http/stream.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Http {

class MockStreamEncoder : public StreamEncoder {
public:
  MockStreamEncoder();
  ~MockStreamEncoder();

  // Http::StreamEncoder
  MOCK_METHOD1(encode100ContinueHeaders, void(const HeaderMap& headers));
  MOCK_METHOD2(encodeHeaders, void(const HeaderMap& headers, bool end_stream));
  MOCK_METHOD2(encodeData, void(Buffer::Instance& data, bool end_stream));
  MOCK_METHOD1(encodeTrailers, void(const HeaderMap& trailers));
  MOCK_METHOD1(encodeMetadata, void(const MetadataMapVector& metadata_map_vector));
  MOCK_METHOD0(getStream, Stream&());

  testing::NiceMock<MockStream> stream_;
};

} // namespace Http
} // namespace Envoy
