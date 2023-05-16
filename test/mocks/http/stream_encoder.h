#pragma once

#include "envoy/http/codec.h"

#include "source/common/http/status.h"

#include "test/mocks/http/stream.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Http {

class MockHttp1StreamEncoderOptions : public Http1StreamEncoderOptions {
public:
  MockHttp1StreamEncoderOptions();
  ~MockHttp1StreamEncoderOptions() override;

  MOCK_METHOD(void, disableChunkEncoding, ());
  MOCK_METHOD(void, enableHalfClose, ());
};

class MockRequestEncoder : public RequestEncoder {
public:
  MockRequestEncoder();
  ~MockRequestEncoder() override;

  // Http::RequestEncoder
  MOCK_METHOD(Status, encodeHeaders, (const RequestHeaderMap& headers, bool end_stream));
  MOCK_METHOD(void, encodeTrailers, (const RequestTrailerMap& trailers));
  MOCK_METHOD(void, enableTcpTunneling, ());

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
  MOCK_METHOD(void, encode1xxHeaders, (const ResponseHeaderMap& headers));
  MOCK_METHOD(void, encodeHeaders, (const ResponseHeaderMap& headers, bool end_stream));
  MOCK_METHOD(void, encodeTrailers, (const ResponseTrailerMap& trailers));
  MOCK_METHOD(void, setRequestDecoder, (RequestDecoder & decoder));
  MOCK_METHOD(void, setDeferredLoggingHeadersAndTrailers,
              (Http::RequestHeaderMapConstSharedPtr request_header_map,
               Http::ResponseHeaderMapConstSharedPtr response_header_map,
               Http::ResponseTrailerMapConstSharedPtr response_trailer_map,
               StreamInfo::StreamInfo& stream_info));

  // Http::StreamEncoder
  MOCK_METHOD(void, encodeData, (Buffer::Instance & data, bool end_stream));
  MOCK_METHOD(void, encodeMetadata, (const MetadataMapVector& metadata_map_vector));
  MOCK_METHOD(Http1StreamEncoderOptionsOptRef, http1StreamEncoderOptions, ());
  MOCK_METHOD(bool, streamErrorOnInvalidHttpMessage, (), (const));
  MOCK_METHOD(Stream&, getStream, (), ());

  testing::NiceMock<MockStream> stream_;
};

} // namespace Http
} // namespace Envoy
