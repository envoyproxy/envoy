#pragma once

#include <cstdint>
#include <memory>
#include <nlohmann/adl_serializer.hpp>
#include <string>

#include "envoy/buffer/buffer.h"
#include "envoy/grpc/status.h"
#include "envoy/http/filter.h"
#include "envoy/http/header_map.h"
#include "envoy/http/metadata_interface.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/buffer/zero_copy_input_stream_impl.h"
#include "source/common/common/logger.h"
#include "source/common/grpc/codec.h"
#include "source/common/protobuf/protobuf.h"
#include "source/extensions/filters/http/grpc_json_reverse_transcoder/filter_config.h"

#include "grpc_transcoding/transcoder.h"
#include "grpc_transcoding/transcoder_input_stream.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace GrpcJsonReverseTranscoder {

class TranscoderInputStreamImpl : public Buffer::ZeroCopyInputStreamImpl,
                                  public google::grpc::transcoding::TranscoderInputStream {
public:
  int64_t BytesAvailable() const override { return buffer_->length() - position_; }

  bool Finished() const override { return finished_; }

  uint64_t bytesStored() const { return buffer_->length(); }
};

class GrpcJsonReverseTranscoderFilter : public Http::StreamFilter,
                                        public Logger::Loggable<Logger::Id::http2> {
public:
  explicit GrpcJsonReverseTranscoderFilter(
      const std::shared_ptr<const GrpcJsonReverseTranscoderConfig>& config)
      : config_(config) {}
  ~GrpcJsonReverseTranscoderFilter() override = default;

  void onDestroy() override {}

  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap&, bool) override;

  Http::FilterDataStatus decodeData(Buffer::Instance&, bool) override;

  Http::FilterTrailersStatus decodeTrailers(Http::RequestTrailerMap&) override {
    return Http::FilterTrailersStatus::Continue;
  }

  void setDecoderFilterCallbacks(Http::StreamDecoderFilterCallbacks&) override;

  Http::Filter1xxHeadersStatus encode1xxHeaders(Http::ResponseHeaderMap&) override {
    return Http::Filter1xxHeadersStatus::Continue;
  }

  Http::FilterHeadersStatus encodeHeaders(Http::ResponseHeaderMap&, bool) override;

  Http::FilterDataStatus encodeData(Buffer::Instance&, bool) override;

  Http::FilterTrailersStatus encodeTrailers(Http::ResponseTrailerMap&) override;

  Http::FilterMetadataStatus encodeMetadata(Http::MetadataMap&) override {
    return Http::FilterMetadataStatus::Continue;
  }

  void setEncoderFilterCallbacks(Http::StreamEncoderFilterCallbacks&) override;

  bool shouldTranscodeResponse() const { return transcoder_ != nullptr; }

  // GrpcJsonReverseTranscoderFilter is neither copyable nor movable.
  GrpcJsonReverseTranscoderFilter(const GrpcJsonReverseTranscoderFilter&) = delete;
  GrpcJsonReverseTranscoderFilter& operator=(const GrpcJsonReverseTranscoderFilter&) = delete;

private:
  // MaybeExpandBufferLimits expands the buffer limits for the request and
  // response if the limits are set in the reverse transcoder config and are
  // greater than the default limits.
  void MaybeExpandBufferLimits() const;
  bool DecoderBufferLimitReached(uint64_t buffer_length) const;
  bool EncoderBufferLimitReached(uint64_t buffer_length) const;
  bool CheckAndRejectIfRequestTranscoderFailed() const;
  bool CheckAndRejectIfResponseTranscoderFailed() const;
  Grpc::Status::GrpcStatus GrpcStatusFromHeaders(Http::ResponseHeaderMap& headers);
  void InitPerRouteConfig();
  // BuildRequestFromHttpBody reads the contents of the data field of the
  // google.api.HttpBody message and builds the request body out of it.
  bool BuildRequestFromHttpBody(Buffer::Instance& data);
  // SendHttpBodyResponse sends the response returned from the upstream server
  // as a google.api.HttpBody message.
  void SendHttpBodyResponse(Buffer::Instance* data);
  void ReplaceAPIVersionInPath();
  bool CreateDataBuffer(const nlohmann::json& payload, Buffer::OwnedImpl& buffer) const;
  absl::Status ExtractHttpAnnotationValues(const Protobuf::MethodDescriptor* method_descriptor);

  Http::StreamDecoderFilterCallbacks* decoder_callbacks_;
  Http::StreamEncoderFilterCallbacks* encoder_callbacks_;

  const GrpcJsonReverseTranscoderConfig* per_route_config_;
  const std::shared_ptr<const GrpcJsonReverseTranscoderConfig> config_;
  std::unique_ptr<google::grpc::transcoding::Transcoder> transcoder_;
  TranscoderInputStreamImpl request_in_;
  TranscoderInputStreamImpl response_in_;

  Buffer::OwnedImpl request_buffer_;
  Buffer::OwnedImpl response_buffer_;
  Buffer::OwnedImpl response_data_;
  Buffer::OwnedImpl error_buffer_;
  Grpc::Decoder decoder_;
  Http::RequestHeaderMap* request_headers_;

  std::string http_request_method_;
  std::string http_request_path_template_;
  std::string http_request_body_field_;

  bool is_request_http_body_;
  bool is_response_http_body_;
  bool is_request_nested_http_body_;

  std::string request_content_type_;
  std::string response_content_type_;
  bool is_non_ok_response_{false};
  bool is_response_passed_through_{false};
  Grpc::Status::GrpcStatus grpc_status_;
};

} // namespace GrpcJsonReverseTranscoder
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
