#pragma once
#include "envoy/http/codec.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Http {

class MockRequestDecoder : public RequestDecoder {
public:
  MockRequestDecoder();
  ~MockRequestDecoder() override;

  void decodeMetadata(MetadataMapPtr&& metadata_map) override { decodeMetadata_(metadata_map); }

  // Http::StreamDecoder
  MOCK_METHOD(void, decodeData, (Buffer::Instance & data, bool end_stream));
  MOCK_METHOD(void, decodeMetadata_, (MetadataMapPtr & metadata_map));
  MOCK_METHOD(void, sendLocalReply,
              (Code code, absl::string_view body,
               const std::function<void(ResponseHeaderMap& headers)>& modify_headers,
               const absl::optional<Grpc::Status::GrpcStatus> grpc_status,
               absl::string_view details));
  MOCK_METHOD(StreamInfo::StreamInfo&, streamInfo, ());

  void decodeHeaders(RequestHeaderMapSharedPtr&& headers, bool end_stream) override {
    decodeHeaders_(headers, end_stream);
  }
  void decodeTrailers(RequestTrailerMapPtr&& trailers) override { decodeTrailers_(trailers); }

  // Http::RequestDecoder
  MOCK_METHOD(void, decodeHeaders_, (RequestHeaderMapSharedPtr & headers, bool end_stream));
  MOCK_METHOD(void, decodeTrailers_, (RequestTrailerMapPtr & trailers));
  MOCK_METHOD(std::list<AccessLog::InstanceSharedPtr>, accessLogHandlers, ());
};

class MockResponseDecoder : public ResponseDecoder {
public:
  MockResponseDecoder();
  ~MockResponseDecoder() override;

  void decodeMetadata(MetadataMapPtr&& metadata_map) override { decodeMetadata_(metadata_map); }

  // Http::StreamDecoder
  MOCK_METHOD(void, decodeData, (Buffer::Instance & data, bool end_stream));
  MOCK_METHOD(void, decodeMetadata_, (MetadataMapPtr & metadata_map));

  void decode1xxHeaders(ResponseHeaderMapPtr&& headers) override { decode1xxHeaders_(headers); }
  void decodeHeaders(ResponseHeaderMapPtr&& headers, bool end_stream) override {
    decodeHeaders_(headers, end_stream);
  }
  void decodeTrailers(ResponseTrailerMapPtr&& trailers) override { decodeTrailers_(trailers); }

  // Http::ResponseDecoder
  MOCK_METHOD(void, decode1xxHeaders_, (ResponseHeaderMapPtr & headers));
  MOCK_METHOD(void, decodeHeaders_, (ResponseHeaderMapPtr & headers, bool end_stream));
  MOCK_METHOD(void, decodeTrailers_, (ResponseTrailerMapPtr & trailers));
  MOCK_METHOD(void, dumpState, (std::ostream&, int), (const));
};

} // namespace Http
} // namespace Envoy
