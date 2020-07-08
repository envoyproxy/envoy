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
              (bool is_grpc_request, Code code, absl::string_view body,
               const std::function<void(ResponseHeaderMap& headers)>& modify_headers,
               bool is_head_request, const absl::optional<Grpc::Status::GrpcStatus> grpc_status,
               absl::string_view details));

  void decodeHeaders(RequestHeaderMapPtr&& headers, bool end_stream) override {
    decodeHeaders_(headers, end_stream);
  }
  void decodeTrailers(RequestTrailerMapPtr&& trailers) override { decodeTrailers_(trailers); }

  // Http::RequestDecoder
  MOCK_METHOD(void, decodeHeaders_, (RequestHeaderMapPtr & headers, bool end_stream));
  MOCK_METHOD(void, decodeTrailers_, (RequestTrailerMapPtr & trailers));
};

class MockResponseDecoder : public ResponseDecoder {
public:
  MockResponseDecoder();
  ~MockResponseDecoder() override;

  void decodeMetadata(MetadataMapPtr&& metadata_map) override { decodeMetadata_(metadata_map); }

  // Http::StreamDecoder
  MOCK_METHOD(void, decodeData, (Buffer::Instance & data, bool end_stream));
  MOCK_METHOD(void, decodeMetadata_, (MetadataMapPtr & metadata_map));

  void decode100ContinueHeaders(ResponseHeaderMapPtr&& headers) override {
    decode100ContinueHeaders_(headers);
  }
  void decodeHeaders(ResponseHeaderMapPtr&& headers, bool end_stream) override {
    decodeHeaders_(headers, end_stream);
  }
  void decodeTrailers(ResponseTrailerMapPtr&& trailers) override { decodeTrailers_(trailers); }

  // Http::ResponseDecoder
  MOCK_METHOD(void, decode100ContinueHeaders_, (ResponseHeaderMapPtr & headers));
  MOCK_METHOD(void, decodeHeaders_, (ResponseHeaderMapPtr & headers, bool end_stream));
  MOCK_METHOD(void, decodeTrailers_, (ResponseTrailerMapPtr & trailers));
};

} // namespace Http
} // namespace Envoy
