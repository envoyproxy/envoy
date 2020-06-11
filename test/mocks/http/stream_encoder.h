#pragma once

#include "envoy/http/codec.h"

#include "test/mocks/http/stream.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Http {

class MockHttp1StreamEncoderOptions : public Http1StreamEncoderOptions {
public:
  MockHttp1StreamEncoderOptions();
  ~MockHttp1StreamEncoderOptions() override;

  MOCK_METHOD(void, disableChunkEncoding, ());
};

class MockRequestEncoder : public RequestEncoder {
public:
  MockRequestEncoder();
  ~MockRequestEncoder() override;

  // Http::RequestEncoder
  MOCK_METHOD(void, encodeHeaders, (const RequestHeaderMap& headers, bool end_stream));
  MOCK_METHOD(void, encodeTrailers, (const RequestTrailerMap& trailers));

  // Http::StreamEncoder
  MOCK_METHOD(void, encodeData, (Buffer::Instance & data, bool end_stream));
  MOCK_METHOD(void, encodeMetadata, (const MetadataMapVector& metadata_map_vector));
  MOCK_METHOD(Http1StreamEncoderOptionsOptRef, http1StreamEncoderOptions, ());
  MOCK_METHOD(Stream&, getStream, (), ());

  testing::NiceMock<MockStream> stream_;
};

class MockResponseEncoder : public ResponseEncoder {
public:
  MockResponseEncoder();
  ~MockResponseEncoder() override;

  // Http::ResponseEncoder
  MOCK_METHOD(void, encode100ContinueHeaders, (const ResponseHeaderMap& headers));
  MOCK_METHOD(void, encodeHeaders, (const ResponseHeaderMap& headers, bool end_stream));
  MOCK_METHOD(void, encodeTrailers, (const ResponseTrailerMap& trailers));

  // Http::StreamEncoder
  MOCK_METHOD(void, encodeData, (Buffer::Instance & data, bool end_stream));
  MOCK_METHOD(void, encodeMetadata, (const MetadataMapVector& metadata_map_vector));
  MOCK_METHOD(Http1StreamEncoderOptionsOptRef, http1StreamEncoderOptions, ());
  MOCK_METHOD(Stream&, getStream, (), ());

  testing::NiceMock<MockStream> stream_;
};

} // namespace Http
} // namespace Envoy
