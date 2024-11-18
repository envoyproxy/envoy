#include "source/extensions/filters/http/grpc_json_reverse_transcoder/filter.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "google/api/http.pb.h"
#include "google/api/httpbody.pb.h"
#include "source/extensions/filters/http/grpc_json_reverse_transcoder/filter_config.h"
#include "source/extensions/filters/http/grpc_json_reverse_transcoder/utils.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/escaping.h"
#include "absl/strings/match.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_replace.h"
#include "absl/strings/string_view.h"
#include "envoy/buffer/buffer.h"
#include "envoy/grpc/status.h"
#include "envoy/http/codes.h"
#include "envoy/http/filter.h"
#include "envoy/http/header_map.h"
#include "source/common/buffer/buffer_impl.h"
#include "source/common/buffer/zero_copy_input_stream_impl.h"
#include "source/common/common/logger.h"
#include "source/common/common/utility.h"
#include "source/common/grpc/codec.h"
#include "source/common/grpc/common.h"
#include "source/common/grpc/status.h"
#include "source/common/http/headers.h"
#include "source/common/http/utility.h"
#include "source/common/protobuf/utility.h"
#include "source/common/singleton/const_singleton.h"
#include "nlohmann/json.hpp"
#include "google/protobuf/io/coded_stream.h"
#include "google/protobuf/io/zero_copy_stream.h"
#include "google/protobuf/io/zero_copy_stream_impl_lite.h"

using ::Envoy::Grpc::Status;
using ::Envoy::Http::Code;
using ::Envoy::Http::FilterDataStatus;
using ::Envoy::Http::FilterHeadersStatus;
using ::Envoy::Http::FilterTrailersStatus;
using ::Envoy::Http::RequestHeaderMap;
using ::Envoy::Http::ResponseHeaderMap;
using ::Envoy::Http::ResponseTrailerMap;
using ::Envoy::Http::StreamDecoderFilterCallbacks;
using ::Envoy::Http::StreamEncoderFilterCallbacks;
using ::Envoy::Protobuf::io::CodedInputStream;
using ::Envoy::Protobuf::io::CodedOutputStream;
using ::Envoy::Protobuf::io::StringOutputStream;
using ::nlohmann::json;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace GrpcJsonReverseTranscoder {

struct RcDetailsValues {
  // The gRPC json transcoder filter failed to transcode when processing request
  // headers. This will generally be accompanied by details about the transcoder
  // failure.
  const std::string grpc_transcode_failed_early =
      "early_grpc_json_reverse_transcode_failure";
  // The gRPC json transcoder filter failed to transcode when processing the
  // request body. This will generally be accompanied by details about the
  // transcoder failure.
  const std::string grpc_transcode_failed =
      "grpc_json_reverse_transcode_failure";
};
using RcDetails = Envoy::ConstSingleton<RcDetailsValues>;

// The placeholder for the API version in the HTTP path.
constexpr std::string_view kHTTPPathAPIVersionPlaceholder = "{$api_version}";


void GrpcJsonReverseTranscoderFilter::ReplaceAPIVersionInPath(const RequestHeaderMap& headers,
                             std::string& path) const {
  if (!per_route_config_->api_version_header_.has_value()) {
    return;
  }
  Envoy::Http::HeaderMap::GetResult api_version_header =
      headers.get(Envoy::Http::LowerCaseString(
        per_route_config_->api_version_header_.value()));
  if (!api_version_header.empty()) {
    absl::string_view api_version =
        api_version_header[0]->value().getStringView();
    path = absl::StrReplaceAll(path,
                               {{kHTTPPathAPIVersionPlaceholder, api_version}});
  }
}

void GrpcJsonReverseTranscoderFilter::InitPerRouteConfig() {
  const auto* route_local =
      Envoy::Http::Utility::resolveMostSpecificPerFilterConfig<
          GrpcJsonReverseTranscoderConfig>(decoder_callbacks_);
  per_route_config_ = route_local ? route_local : config_.get();
}

void GrpcJsonReverseTranscoderFilter::MaybeExpandBufferLimits() {
  const uint32_t max_request_body_size =
      per_route_config_->max_request_body_size_.value_or(0);
  const uint32_t max_response_body_size =
      per_route_config_->max_response_body_size_.value_or(0);
  if (max_request_body_size > decoder_callbacks_->decoderBufferLimit()) {
    decoder_callbacks_->setDecoderBufferLimit(max_request_body_size);
  }
  if (max_response_body_size > encoder_callbacks_->encoderBufferLimit()) {
    encoder_callbacks_->setEncoderBufferLimit(max_response_body_size);
  }
}

bool GrpcJsonReverseTranscoderFilter::DecoderBufferLimitReached(
    uint64_t buffer_length) {
  const uint32_t max_size = per_route_config_->max_request_body_size_.value_or(
      decoder_callbacks_->decoderBufferLimit());
  if (buffer_length > max_size) {
    ENVOY_STREAM_LOG(
        error,
        "Request size has exceeded the maximum allowed request size limit",
        *decoder_callbacks_);
    decoder_callbacks_->sendLocalReply(
        Code::PayloadTooLarge, "Request entity too large", nullptr,
        Status::WellKnownGrpcStatus::ResourceExhausted,
        absl::StrCat(RcDetails::get().grpc_transcode_failed,
                     "{request_buffer_size_limit_reached}"));
    return true;
  }
  return false;
}

bool GrpcJsonReverseTranscoderFilter::EncoderBufferLimitReached(
    uint64_t buffer_length) {
  const uint32_t max_size = per_route_config_->max_response_body_size_.value_or(
      encoder_callbacks_->encoderBufferLimit());
  if (buffer_length > max_size) {
    ENVOY_STREAM_LOG(
        error,
        "Response size has exceeded the maximum allowed response size limit",
        *encoder_callbacks_);
    encoder_callbacks_->sendLocalReply(
        Code::InternalServerError, "Response entity too large", nullptr,
        Status::WellKnownGrpcStatus::Internal,
        absl::StrCat(RcDetails::get().grpc_transcode_failed,
                     "{response_buffer_size_limit_reached}"));
    return true;
  }
  return false;
}

bool GrpcJsonReverseTranscoderFilter::
    CheckAndRejectIfRequestTranscoderFailed() {
  const auto& status = transcoder_->RequestStatus();
  if (status.ok()) return false;
  ENVOY_STREAM_LOG(error, "Request transcoding failed: {}", *decoder_callbacks_,
                   status.message());
  decoder_callbacks_->sendLocalReply(
      Code::BadRequest, status.message(), nullptr,
      Status::WellKnownGrpcStatus::InvalidArgument,
      absl::StrCat(RcDetails::get().grpc_transcode_failed, "{",
                   Envoy::StringUtil::replaceAllEmptySpace(
                       Envoy::MessageUtil::codeEnumToString(status.code())),
                   "}"));
  return true;
}

bool GrpcJsonReverseTranscoderFilter::
    CheckAndRejectIfResponseTranscoderFailed() {
  const auto& status = transcoder_->ResponseStatus();
  if (status.ok()) return false;
  ENVOY_STREAM_LOG(error, "Response transcoding failed: {}",
                   *encoder_callbacks_, status.message());
  encoder_callbacks_->sendLocalReply(
      Code::InternalServerError, status.message(), nullptr,
      Status::WellKnownGrpcStatus::Internal,
      absl::StrCat(RcDetails::get().grpc_transcode_failed, "{",
                   Envoy::StringUtil::replaceAllEmptySpace(
                       Envoy::MessageUtil::codeEnumToString(status.code())),
                   "}"));
  return true;
}

bool GrpcJsonReverseTranscoderFilter::ReadToBuffer(
    Envoy::Protobuf::io::ZeroCopyInputStream& stream,
    Envoy::Buffer::Instance& buffer) {
  const void* out;
  int size;
  while (stream.Next(&out, &size)) {
    if (size == 0) return true;
    buffer.add(out, size);
  }
  return false;
}

Status::GrpcStatus GrpcJsonReverseTranscoderFilter::GrpcStatusFromHeaders(
    ResponseHeaderMap& headers) {
  const auto http_response_status =
      Envoy::Http::Utility::getResponseStatus(headers);
  if (http_response_status == 200) {
    return Status::WellKnownGrpcStatus::Ok;
  } else {
    is_non_ok_response_ = true;
    return Envoy::Grpc::Utility::httpToGrpcStatus(http_response_status);
  }
}

bool GrpcJsonReverseTranscoderFilter::BuildRequestFromHttpBody(
    RequestHeaderMap& headers, Envoy::Buffer::Instance& data) {
  std::vector<Envoy::Grpc::Frame> frames;
  static_cast<void>(decoder_.decode(data, frames));
  if (frames.empty()) {
    return false;
  }
  google::api::HttpBody http_body;
  for (auto& frame : frames) {
    if (frame.length_ > 0) {
      http_body.Clear();
      Buffer::ZeroCopyInputStreamImpl stream(std::move(frame.data_));
      CodedInputStream coded_stream(&stream);
      if (!http_body.MergeFromCodedStream(&coded_stream)) {
        decoder_callbacks_->resetStream();
        return true;
      }
      const std::string body = static_cast<std::string>(http_body.data());
      data.add(body);
      headers.setContentType(http_body.content_type());
      headers.setContentLength(body.size());
      headers.setPath(request_params_.http_rule_path);
    }
  }
  return true;
}

void GrpcJsonReverseTranscoderFilter::AppendHttpBodyEnvelope(
    Envoy::Buffer::Instance& output, std::string content_type,
    uint64_t content_length) {
  // Manually encode the protobuf envelope for the body.
  // See https://developers.google.com/protocol-buffers/docs/encoding#embedded
  // for wire format.

  std::string proto_envelope;
  {
    // For memory safety, the StringOutputStream needs to be destroyed before
    // we read the string.
    const uint32_t length_delimited_field = 2;
    const uint32_t http_body_field_tag =
        (google::api::HttpBody::kDataFieldNumber << 3) | length_delimited_field;

    ::google::api::HttpBody body;
    body.set_content_type(std::move(content_type));

    uint64_t envelope_size =
        body.ByteSizeLong() +
        CodedOutputStream::VarintSize32(http_body_field_tag) +
        CodedOutputStream::VarintSize64(content_length);
    std::vector<uint32_t> message_sizes;
    message_sizes.reserve(method_info_.response_body_field_path.size());
    for (auto it = method_info_.response_body_field_path.rbegin();
         it != method_info_.response_body_field_path.rend(); ++it) {
      const Envoy::ProtobufWkt::Field* field = *it;
      const uint64_t message_size = envelope_size + content_length;
      const uint32_t field_tag =
          (field->number() << 3) | length_delimited_field;
      const uint64_t field_size =
          CodedOutputStream::VarintSize32(field_tag) +
          CodedOutputStream::VarintSize64(message_size);
      message_sizes.push_back(message_size);
      envelope_size += field_size;
    }
    std::reverse(message_sizes.begin(), message_sizes.end());

    proto_envelope.reserve(envelope_size);

    StringOutputStream string_stream(&proto_envelope);
    CodedOutputStream coded_stream(&string_stream);

    // Serialize body field definition manually to avoid the copy of the body.
    for (size_t i = 0; i < method_info_.response_body_field_path.size(); ++i) {
      const Envoy::ProtobufWkt::Field* field =
          method_info_.response_body_field_path[i];
      const uint32_t field_number =
          (field->number() << 3) | length_delimited_field;
      const uint64_t message_size = message_sizes[i];
      coded_stream.WriteTag(field_number);
      coded_stream.WriteVarint64(message_size);
    }
    body.SerializeToCodedStream(&coded_stream);
    coded_stream.WriteTag(http_body_field_tag);
    coded_stream.WriteVarint64(content_length);
  }

  output.add(proto_envelope);
}

void GrpcJsonReverseTranscoderFilter::SendHttpBodyResponse(
    Envoy::Buffer::Instance* data) {
  if (response_data_.length() == 0) {
    return;
  }
  Envoy::Buffer::OwnedImpl message_payload;
  AppendHttpBodyEnvelope(message_payload, response_content_type_,
                         response_data_.length());
  response_content_type_.clear();
  message_payload.move(response_data_);
  Envoy::Grpc::Encoder().prependFrameHeader(Envoy::Grpc::GRPC_FH_DEFAULT,
                                            message_payload);
  if (data) {
    data->move(message_payload);
  } else {
    encoder_callbacks_->addEncodedData(message_payload, false);
  }
}

void GrpcJsonReverseTranscoderFilter::setDecoderFilterCallbacks(
    StreamDecoderFilterCallbacks& callbacks) {
  decoder_callbacks_ = &callbacks;
}

void GrpcJsonReverseTranscoderFilter::setEncoderFilterCallbacks(
    StreamEncoderFilterCallbacks& callbacks) {
  encoder_callbacks_ = &callbacks;
}

FilterHeadersStatus GrpcJsonReverseTranscoderFilter::decodeHeaders(
    RequestHeaderMap& headers, bool end_stream) {
  // Short circuit if header only.
  if (end_stream || !Envoy::Grpc::Common::isGrpcRequestHeaders(headers)) {
    ENVOY_STREAM_LOG(info, "Request headers are passed through",
                     *decoder_callbacks_);
    return FilterHeadersStatus::Continue;
  }
  ENVOY_STREAM_LOG(info, "Initializing per route config, if available",
                   *decoder_callbacks_);
  InitPerRouteConfig();

  const auto status = per_route_config_->CreateTranscoder(
      headers.getPathValue(), request_in_, response_in_, transcoder_,
      request_params_, method_info_);

  if (!status.ok()) {
    ENVOY_STREAM_LOG(error, "Failed to create a transcoder instance: {}",
                     *decoder_callbacks_, status.message());
    decoder_callbacks_->sendLocalReply(
        Code::InternalServerError, status.message(), nullptr,
        Status::WellKnownGrpcStatus::Internal,
        absl::StrCat(RcDetails::get().grpc_transcode_failed_early, "{",
                     Envoy::StringUtil::replaceAllEmptySpace(
                         Envoy::MessageUtil::codeEnumToString(status.code())),
                     "}"));
    return FilterHeadersStatus::StopIteration;
  }

  ReplaceAPIVersionInPath(headers, request_params_.http_rule_path);

  MaybeExpandBufferLimits();

  request_content_type_ = headers.getContentTypeValue();
  headers.setContentType(Envoy::Http::Headers::get().ContentTypeValues.Json);
  headers.setMethod(request_params_.method);
  headers.removeContentLength();
  request_headers_ = &headers;

  decoder_callbacks_->downstreamCallbacks()->clearRouteCache();

  return FilterHeadersStatus::StopIteration;
}

FilterDataStatus GrpcJsonReverseTranscoderFilter::decodeData(
    Envoy::Buffer::Instance& data, bool end_stream) {
  if (!transcoder_) {
    ENVOY_STREAM_LOG(info, "Request data is passed through",
                     *decoder_callbacks_);
    return FilterDataStatus::Continue;
  }
  if (method_info_.is_request_http_body) {
    ENVOY_STREAM_LOG(debug, "Request message is of type HttpBody",
                     *decoder_callbacks_);
    if (absl::StrContains(request_params_.http_rule_path, "{")) {
      ENVOY_STREAM_LOG(error,
                       "The path for the request message of type "
                       "google.api.HttpBody shouldn't contain any variables "
                       "except the API version",
                       *decoder_callbacks_);
      decoder_callbacks_->sendLocalReply(
          Code::BadRequest,
          "The path for the request message of type google.api.HttpBody "
          "shouldn't contain any variables except the API version",
          nullptr, Status::WellKnownGrpcStatus::InvalidArgument,
          absl::StrCat(RcDetails::get().grpc_transcode_failed,
                       "{failed_to_create_request_path}"));
      return FilterDataStatus::StopIterationNoBuffer;
    }
    if (BuildRequestFromHttpBody(*request_headers_, data)) {
      return FilterDataStatus::Continue;
    }
    return FilterDataStatus::StopIterationAndBuffer;
  }
  request_in_.move(data);
  if (DecoderBufferLimitReached(request_in_.bytesStored())) {
    return FilterDataStatus::StopIterationNoBuffer;
  }

  if (CheckAndRejectIfRequestTranscoderFailed()) {
    return FilterDataStatus::StopIterationNoBuffer;
  }

  ReadToBuffer(*transcoder_->RequestOutput(), request_buffer_);

  if (!end_stream) {
    return FilterDataStatus::StopIterationNoBuffer;
  }
  request_in_.finish();
  if (!json::accept(request_buffer_.toString())) {
    ENVOY_STREAM_LOG(
        error,
        "Failed to parse the transcoded request to build the path header.",
        *decoder_callbacks_);
    decoder_callbacks_->sendLocalReply(
        Code::InternalServerError, "Failed to parse the transcoded request",
        nullptr, Status::WellKnownGrpcStatus::Internal,
        absl::StrCat(RcDetails::get().grpc_transcode_failed,
                     "{failed_to_parse_request_body}"));
    return FilterDataStatus::StopIterationNoBuffer;
  }

  json payload = json::parse(request_buffer_.toString());

  // Check if there is a request body to be sent with the HTTP request. If yes,
  // is it the entire gRPC request message or just a field from the request
  // message.
  if (!request_params_.http_body_field.empty()) {
    if (request_params_.http_body_field == "*") {
      ENVOY_STREAM_LOG(debug,
                       "Using the entire gRPC message as the request body",
                       *decoder_callbacks_);
      data.move(request_buffer_);
    } else {
      if (!payload.contains(request_params_.http_body_field)) {
        ENVOY_STREAM_LOG(
            error,
            "Failed to find the field, `{}`, from the gRPC request message",
            *decoder_callbacks_, request_params_.http_body_field);
        decoder_callbacks_->sendLocalReply(
            Code::BadRequest,
            "Failed to get the request body from the gRPC message", nullptr,
            Status::WellKnownGrpcStatus::InvalidArgument,
            absl::StrCat(RcDetails::get().grpc_transcode_failed,
                         "{failed_to_create_request_body}"));
        return FilterDataStatus::StopIterationNoBuffer;
      }
      Envoy::Buffer::OwnedImpl buffer;
      if (method_info_.is_request_nested_http_body) {
        std::string decoded_body;
        if (!absl::WebSafeBase64Unescape(
                payload[request_params_.http_body_field]["data"]
                    .get<std::string>(),
                &decoded_body)) {
          ENVOY_STREAM_LOG(
              error, "Failed to decode the request body from the gRPC message",
              *decoder_callbacks_);
          decoder_callbacks_->sendLocalReply(
              Code::BadRequest,
              "Failed to decode the request body from the gRPC message",
              nullptr, Status::WellKnownGrpcStatus::InvalidArgument,
              absl::StrCat(RcDetails::get().grpc_transcode_failed,
                           "{failed_to_decode_request_body}"));
          return FilterDataStatus::StopIterationNoBuffer;
        }
        buffer.add(decoded_body);
        request_headers_->setContentType(
            payload[request_params_.http_body_field]["content_type"].dump());
      } else {
        buffer.add(payload[request_params_.http_body_field].dump());
      }
      ENVOY_STREAM_LOG(
          debug,
          "Using the field, {}, from the gRPC message as the request body",
          *decoder_callbacks_, request_params_.http_body_field);
      data.move(buffer);
    }
  }
  absl::StatusOr<std::string> path = BuildPath(
      payload, request_params_.http_rule_path, request_params_.http_body_field);
  if (!path.ok()) {
    ENVOY_STREAM_LOG(error, "Failed to build the path header: {}",
                     *decoder_callbacks_, path.status().message());
    decoder_callbacks_->sendLocalReply(
        Code::BadRequest, path.status().message(), nullptr,
        Status::WellKnownGrpcStatus::InvalidArgument,
        absl::StrCat(RcDetails::get().grpc_transcode_failed,
                     "{failed_to_build_request_path}"));
    return FilterDataStatus::StopIterationNoBuffer;
  }
  request_headers_->setPath(path.value());
  return FilterDataStatus::Continue;
}

FilterHeadersStatus GrpcJsonReverseTranscoderFilter::encodeHeaders(
    ResponseHeaderMap& headers, bool end_stream) {
  if (Envoy::Grpc::Common::isGrpcResponseHeaders(headers, end_stream)) {
    ENVOY_STREAM_LOG(info, "Response type is gRPC", *encoder_callbacks_);
    is_response_passed_through_ = true;
  }
  if (!transcoder_ || is_response_passed_through_) {
    ENVOY_STREAM_LOG(info, "Response headers are passed through",
                     *encoder_callbacks_);
    return FilterHeadersStatus::Continue;
  }
  response_content_type_ = headers.getContentTypeValue();
  headers.setContentType(request_content_type_);
  grpc_status_ = GrpcStatusFromHeaders(headers);

  // gRPC client always expect the HTTP status to be 200.
  headers.setStatus(static_cast<uint32_t>(Code::OK));

  absl::string_view content_length = headers.getContentLengthValue();
  if (!content_length.empty()) {
    uint64_t length;
    if (absl::SimpleAtoi(content_length, &length) && length != 0) {
      ENVOY_STREAM_LOG(
          debug,
          "Adjusting the content length header as per the gRPC response.",
          *encoder_callbacks_);
      headers.setContentLength(length + Envoy::Grpc::GRPC_FRAME_HEADER_SIZE);
    }
  }

  if (end_stream) {
    Envoy::Buffer::OwnedImpl buffer;
    Envoy::Grpc::Encoder().prependFrameHeader(Envoy::Grpc::GRPC_FH_DEFAULT,
                                              buffer, 0);
    encoder_callbacks_->addEncodedData(buffer, false);

    headers.setContentLength(Envoy::Grpc::GRPC_FRAME_HEADER_SIZE);

    auto& trailers = encoder_callbacks_->addEncodedTrailers();
    trailers.setGrpcStatus(grpc_status_);

    response_in_.finish();
  }

  return FilterHeadersStatus::Continue;
}

FilterDataStatus GrpcJsonReverseTranscoderFilter::encodeData(
    Envoy::Buffer::Instance& data, bool end_stream) {
  if (!transcoder_ || is_response_passed_through_) {
    ENVOY_STREAM_LOG(info, "Response data is passed through",
                     *encoder_callbacks_);
    return FilterDataStatus::Continue;
  }

  if (is_non_ok_response_) {
    ENVOY_STREAM_LOG(debug,
                     "Buffering the payload of non-200 response to be added to "
                     "the trailers later.",
                     *encoder_callbacks_);
    error_buffer_.move(data);
  } else if (method_info_.is_response_http_body) {
    response_data_.move(data);
    if (EncoderBufferLimitReached(response_data_.length())) {
      return FilterDataStatus::StopIterationNoBuffer;
    }
  } else {
    response_in_.move(data);
    if (EncoderBufferLimitReached(response_in_.bytesStored() +
                                  response_buffer_.length())) {
      return FilterDataStatus::StopIterationNoBuffer;
    }

    ReadToBuffer(*transcoder_->ResponseOutput(), response_buffer_);

    if (CheckAndRejectIfResponseTranscoderFailed()) {
      return FilterDataStatus::StopIterationNoBuffer;
    }
  }

  if (!end_stream) {
    return FilterDataStatus::StopIterationNoBuffer;
  }

  response_in_.finish();
  auto& trailers = encoder_callbacks_->addEncodedTrailers();
  trailers.setGrpcStatus(grpc_status_);
  if (is_non_ok_response_) {
    ENVOY_STREAM_LOG(debug, "Adding the response error payload to the trailers",
                     *encoder_callbacks_);
    trailers.setGrpcMessage(BuildGrpcMessage(error_buffer_));
  } else if (method_info_.is_response_http_body) {
    SendHttpBodyResponse(&data);
  } else {
    data.move(response_buffer_);
  }
  return FilterDataStatus::Continue;
}

FilterTrailersStatus GrpcJsonReverseTranscoderFilter::encodeTrailers(
    ResponseTrailerMap& trailers) {
  if (!transcoder_ || is_response_passed_through_) {
    ENVOY_STREAM_LOG(info, "Response trailers are passed through",
                     *encoder_callbacks_);
    return FilterTrailersStatus::Continue;
  }
  trailers.setGrpcStatus(grpc_status_);
  if (is_non_ok_response_) {
    ENVOY_STREAM_LOG(debug, "Adding the response error payload to the trailers",
                     *encoder_callbacks_);
    trailers.setGrpcMessage(BuildGrpcMessage(error_buffer_));
  } else if (method_info_.is_response_http_body) {
    SendHttpBodyResponse(nullptr);
  } else {
    encoder_callbacks_->addEncodedData(response_buffer_, false);
  }
  response_in_.finish();

  return FilterTrailersStatus::Continue;
}

}  // namespace GrpcJsonReverseTranscoder
}  // namespace HttpFilters
}  // namespace Extensions
}  // namespace Envoy
