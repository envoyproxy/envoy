#pragma once

#include "envoy/http/codec.h"

#include "test/mocks/http/stream.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Http {

class MockStreamEncoder : public StreamEncoder {
public:
  MockStreamEncoder();
  ~MockStreamEncoder() override;

  // Http::StreamEncoder
  MOCK_METHOD(void, encode100ContinueHeaders, (const HeaderMap& headers));
  MOCK_METHOD(void, encodeHeaders, (const HeaderMap& headers, bool end_stream));
  MOCK_METHOD(void, encodeData, (Buffer::Instance & data, bool end_stream));
  MOCK_METHOD(void, encodeTrailers, (const HeaderMap& trailers));
  MOCK_METHOD(void, encodeMetadata, (const MetadataMapVector& metadata_map_vector));
  MOCK_METHOD(Stream&, getStream, ());

  testing::NiceMock<MockStream> stream_;
};

} // namespace Http
} // namespace Envoy
