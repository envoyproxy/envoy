#include "source/extensions/filters/http/grpc_json_reverse_transcoder/filter.h"

#include <cstdint>
#include <initializer_list>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "envoy/buffer/buffer.h"
#include "envoy/grpc/status.h"
#include "envoy/http/codes.h"
#include "envoy/http/filter.h"
#include "envoy/http/header_map.h"
#include "envoy/http/stream_reset_handler.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/buffer/zero_copy_input_stream_impl.h"
#include "source/common/common/logger.h"
#include "source/common/common/utility.h"
#include "source/common/grpc/codec.h"
#include "source/common/grpc/common.h"
#include "source/common/grpc/status.h"
#include "source/common/http/headers.h"
#include "source/common/http/utility.h"
#include "source/common/protobuf/protobuf.h"
#include "source/common/protobuf/utility.h"
#include "source/common/singleton/const_singleton.h"
#include "source/extensions/filters/http/grpc_json_reverse_transcoder/filter_config.h"
#include "source/extensions/filters/http/grpc_json_reverse_transcoder/utils.h"

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/escaping.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_replace.h"
#include "absl/strings/string_view.h"
#include "google/api/annotations.pb.h"
#include "google/api/http.pb.h"
#include "google/api/httpbody.pb.h"
#include "nlohmann/json.hpp"

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
using ::google::api::HttpRule;
using ::nlohmann::json;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace GrpcJsonReverseTranscoder {

namespace {

// AppendHttpBodyEnvelope wraps the response returned from the upstream server
// in a google.api.HttpBody message.
void AppendHttpBodyEnvelope(Buffer::Instance& output, std::string content_type,
                            uint64_t content_length) {
  // Manually encode the protobuf envelope for the body.
  // See https://developers.google.com/protocol-buffers/docs/encoding#embedded
  // for wire format.

  std::string proto_envelope;
  {
    // For memory safety, the StringOutputStream needs to be destroyed before
    // we read the string.

    // http_body_tag is the protobuf field tag with the higher bits representing the
    // field number and the lower bits representing the wire type. The wire type in
    // this case is 2, i.e. length-delimited record.
    constexpr uint32_t http_body_tag = (google::api::HttpBody::kDataFieldNumber << 3) | 2;

    ::google::api::HttpBody body;
    body.set_content_type(std::move(content_type));

    uint64_t envelope_size = body.ByteSizeLong() + CodedOutputStream::VarintSize32(http_body_tag) +
                             CodedOutputStream::VarintSize64(content_length);

    proto_envelope.reserve(envelope_size);

    StringOutputStream string_stream(&proto_envelope);
    CodedOutputStream coded_stream(&string_stream);

    body.SerializeToCodedStream(&coded_stream);
    coded_stream.WriteTag(http_body_tag);
    coded_stream.WriteVarint64(content_length);
  }

  output.add(proto_envelope);
}

bool ReadToBuffer(Protobuf::io::ZeroCopyInputStream& stream, Buffer::Instance& buffer) {
  const void* out;
  int size;
  while (stream.Next(&out, &size)) {
    if (size == 0)
      return true;
    buffer.add(out, size);
  }
  return false;
}

} // namespace

struct RcDetailsValues {
  // The gRPC json transcoder filter failed to transcode when processing request
  // headers. This will generally be accompanied by details about the transcoder
  // failure.
  const std::string grpc_transcode_failed_early = "early_grpc_json_reverse_transcode_failure";
  // The gRPC json transcoder filter failed to transcode when processing the
  // request body. This will generally be accompanied by details about the
  // transcoder failure.
  const std::string grpc_transcode_failed = "grpc_json_reverse_transcode_failure";
};
using RcDetails = ConstSingleton<RcDetailsValues>;

// The placeholder for the API version in the HTTP path.
constexpr absl::string_view kHTTPPathAPIVersionPlaceholder = "{$api_version}";

void GrpcJsonReverseTranscoderFilter::ReplaceAPIVersionInPath() {
  if (!per_route_config_->api_version_header_.has_value()) {
    return;
  }
  Http::HeaderMap::GetResult api_version_header =
      request_headers_->get(Http::LowerCaseString(per_route_config_->api_version_header_.value()));
  if (!api_version_header.empty()) {
    absl::string_view api_version = api_version_header[0]->value().getStringView();
    http_request_path_template_ = absl::StrReplaceAll(
        http_request_path_template_, {{kHTTPPathAPIVersionPlaceholder, api_version}});
  }
}

void GrpcJsonReverseTranscoderFilter::InitPerRouteConfig() {
  const auto* route_local =
      Http::Utility::resolveMostSpecificPerFilterConfig<GrpcJsonReverseTranscoderConfig>(
          decoder_callbacks_);
  per_route_config_ = route_local ? route_local : config_.get();
}

void GrpcJsonReverseTranscoderFilter::MaybeExpandBufferLimits() const {
  const uint32_t max_request_body_size = per_route_config_->max_request_body_size_.value_or(0);
  const uint32_t max_response_body_size = per_route_config_->max_response_body_size_.value_or(0);
  if (max_request_body_size > decoder_callbacks_->decoderBufferLimit()) {
    decoder_callbacks_->setDecoderBufferLimit(max_request_body_size);
  }
  if (max_response_body_size > encoder_callbacks_->encoderBufferLimit()) {
    encoder_callbacks_->setEncoderBufferLimit(max_response_body_size);
  }
}

bool GrpcJsonReverseTranscoderFilter::DecoderBufferLimitReached(uint64_t buffer_length) const {
  const uint32_t max_size =
      per_route_config_->max_request_body_size_.value_or(decoder_callbacks_->decoderBufferLimit());
  if (buffer_length > max_size) {
    ENVOY_STREAM_LOG(error, "Request size has exceeded the maximum allowed request size limit",
                     *decoder_callbacks_);
    decoder_callbacks_->sendLocalReply(Code::PayloadTooLarge, "Request entity too large", nullptr,
                                       Status::WellKnownGrpcStatus::ResourceExhausted,
                                       absl::StrCat(RcDetails::get().grpc_transcode_failed,
                                                    "{request_buffer_size_limit_reached}"));
    return true;
  }
  return false;
}

bool GrpcJsonReverseTranscoderFilter::EncoderBufferLimitReached(uint64_t buffer_length) const {
  const uint32_t max_size =
      per_route_config_->max_response_body_size_.value_or(encoder_callbacks_->encoderBufferLimit());
  if (buffer_length > max_size) {
    ENVOY_STREAM_LOG(error, "Response size has exceeded the maximum allowed response size limit",
                     *encoder_callbacks_);
    encoder_callbacks_->sendLocalReply(Code::InternalServerError, "Response entity too large",
                                       nullptr, Status::WellKnownGrpcStatus::Internal,
                                       absl::StrCat(RcDetails::get().grpc_transcode_failed,
                                                    "{response_buffer_size_limit_reached}"));
    return true;
  }
  return false;
}

bool GrpcJsonReverseTranscoderFilter::CheckAndRejectIfRequestTranscoderFailed() const {
  const auto& status = transcoder_->RequestStatus();
  if (status.ok())
    return false;
  decoder_callbacks_->sendLocalReply(
      Code::BadRequest, status.message(), nullptr, Status::WellKnownGrpcStatus::InvalidArgument,
      absl::StrCat(RcDetails::get().grpc_transcode_failed, "{",
                   StringUtil::replaceAllEmptySpace(MessageUtil::codeEnumToString(status.code())),
                   "}"));
  return true;
}

bool GrpcJsonReverseTranscoderFilter::CheckAndRejectIfResponseTranscoderFailed() const {
  const auto& status = transcoder_->ResponseStatus();
  if (status.ok())
    return false;
  ENVOY_STREAM_LOG(error, "Response transcoding failed: {}", *encoder_callbacks_, status.message());
  encoder_callbacks_->sendLocalReply(
      Code::InternalServerError, status.message(), nullptr, Status::WellKnownGrpcStatus::Internal,
      absl::StrCat(RcDetails::get().grpc_transcode_failed, "{",
                   StringUtil::replaceAllEmptySpace(MessageUtil::codeEnumToString(status.code())),
                   "}"));
  return true;
}

Status::GrpcStatus
GrpcJsonReverseTranscoderFilter::GrpcStatusFromHeaders(ResponseHeaderMap& headers) {
  const auto http_response_status = Http::Utility::getResponseStatus(headers);
  if (http_response_status >= 200 && http_response_status <= 299) {
    return Status::WellKnownGrpcStatus::Ok;
  } else {
    is_non_ok_response_ = true;
    return Grpc::Utility::httpToGrpcStatus(http_response_status);
  }
}

bool GrpcJsonReverseTranscoderFilter::BuildRequestFromHttpBody(Buffer::Instance& data) {
  std::vector<Grpc::Frame> frames;
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
        decoder_callbacks_->resetStream(Http::StreamResetReason::LocalReset,
                                        "Failed to decode the `google.api.HttpBody` message");
        return true;
      }
      const std::string body = static_cast<std::string>(http_body.data());
      data.add(body);
      request_headers_->setContentType(http_body.content_type());
      request_headers_->setContentLength(body.size());
      request_headers_->setPath(http_request_path_template_);
    }
  }
  return true;
}

void GrpcJsonReverseTranscoderFilter::SendHttpBodyResponse(Buffer::Instance* data) {
  if (response_data_.length() == 0) {
    return;
  }
  Buffer::OwnedImpl message_payload;
  AppendHttpBodyEnvelope(message_payload, response_content_type_, response_data_.length());
  response_content_type_.clear();
  message_payload.move(response_data_);
  Grpc::Encoder().prependFrameHeader(Grpc::GRPC_FH_DEFAULT, message_payload);
  if (data) {
    data->move(message_payload);
  } else {
    encoder_callbacks_->addEncodedData(message_payload, false);
  }
}

bool GrpcJsonReverseTranscoderFilter::CreateDataBuffer(const json& payload,
                                                       Buffer::OwnedImpl& buffer) const {
  if (!payload.contains(http_request_body_field_)) {
    ENVOY_STREAM_LOG(error, "Failed to find the field, `{}`, from the gRPC request message",
                     *decoder_callbacks_, http_request_body_field_);
    decoder_callbacks_->sendLocalReply(
        Code::BadRequest, "Failed to get the request body from the gRPC message", nullptr,
        Status::WellKnownGrpcStatus::InvalidArgument,
        absl::StrCat(RcDetails::get().grpc_transcode_failed, "{failed_to_create_request_body}"));
    return false;
  }
  if (is_request_nested_http_body_) {
    std::string decoded_body;
    if (!absl::WebSafeBase64Unescape(payload[http_request_body_field_]["data"].get<std::string>(),
                                     &decoded_body)) {
      decoder_callbacks_->sendLocalReply(
          Code::BadRequest, "Failed to decode the request body from the gRPC message", nullptr,
          Status::WellKnownGrpcStatus::InvalidArgument,
          absl::StrCat(RcDetails::get().grpc_transcode_failed, "{failed_to_decode_request_body}"));
      return false;
    }
    buffer.add(decoded_body);
    if (payload[http_request_body_field_].contains("content_type")) {
      request_headers_->setContentType(
          payload[http_request_body_field_]["content_type"].get<std::string>());
    }
  } else {
    buffer.add(payload[http_request_body_field_].dump());
  }
  return true;
}

absl::Status GrpcJsonReverseTranscoderFilter::ExtractHttpAnnotationValues(
    const Protobuf::MethodDescriptor* method_descriptor) {
  HttpRule http_rule = method_descriptor->options().GetExtension(google::api::http);
  switch (http_rule.pattern_case()) {
  case HttpRule::PatternCase::kGet:
    http_request_method_ = Http::Headers::get().MethodValues.Get;
    http_request_path_template_ = http_rule.get();
    break;
  case HttpRule::PatternCase::kPost:
    http_request_method_ = Http::Headers::get().MethodValues.Post;
    http_request_path_template_ = http_rule.post();
    break;
  case HttpRule::PatternCase::kPut:
    http_request_method_ = Http::Headers::get().MethodValues.Put;
    http_request_path_template_ = http_rule.put();
    break;
  case HttpRule::PatternCase::kDelete:
    http_request_method_ = Http::Headers::get().MethodValues.Delete;
    http_request_path_template_ = http_rule.delete_();
    break;
  case HttpRule::PatternCase::kPatch:
    http_request_method_ = Http::Headers::get().MethodValues.Patch;
    http_request_path_template_ = http_rule.patch();
    break;
  case HttpRule::PatternCase::kCustom:
    http_request_method_ = http_rule.custom().kind();
    http_request_path_template_ = http_rule.custom().path();
    break;
  default:
    ENVOY_STREAM_LOG(error,
                     "The grpc method, {}, is either missing the http annotation or is configured "
                     "with an invalid http verb",
                     *decoder_callbacks_, method_descriptor->name());
    return absl::InvalidArgumentError("Invalid or missing http annotations");
  }
  http_request_body_field_ = http_rule.body();
  return absl::OkStatus();
}

void GrpcJsonReverseTranscoderFilter::setDecoderFilterCallbacks(
    StreamDecoderFilterCallbacks& callbacks) {
  decoder_callbacks_ = &callbacks;
}

void GrpcJsonReverseTranscoderFilter::setEncoderFilterCallbacks(
    StreamEncoderFilterCallbacks& callbacks) {
  encoder_callbacks_ = &callbacks;
}

FilterHeadersStatus GrpcJsonReverseTranscoderFilter::decodeHeaders(RequestHeaderMap& headers,
                                                                   bool end_stream) {
  // Short circuit if header only.
  if (end_stream || !Grpc::Common::isGrpcRequestHeaders(headers)) {
    ENVOY_STREAM_LOG(info, "Request headers are passed through", *decoder_callbacks_);
    return FilterHeadersStatus::Continue;
  }
  ENVOY_STREAM_LOG(debug, "Initializing per route config, if available", *decoder_callbacks_);
  InitPerRouteConfig();

  absl::string_view path = headers.getPathValue();
  const auto* method_descriptor = per_route_config_->GetMethodDescriptor(path);
  if (method_descriptor == nullptr) {
    ENVOY_STREAM_LOG(error, "Couldn't find a gRPC method matching {}", *decoder_callbacks_, path);
    decoder_callbacks_->sendLocalReply(
        Code::BadRequest, absl::StrCat("Couldn't find a gRPC method matching: ", path), nullptr,
        Status::WellKnownGrpcStatus::InvalidArgument,
        absl::StrCat(RcDetails::get().grpc_transcode_failed_early, "{",
                     StringUtil::replaceAllEmptySpace(
                         MessageUtil::codeEnumToString(absl::StatusCode::kNotFound)),
                     "}"));
    return FilterHeadersStatus::StopIteration;
  }
  if (method_descriptor->client_streaming() || method_descriptor->server_streaming()) {
    ENVOY_STREAM_LOG(error, "The HTTP REST backend doesn't support request/response streaming",
                     *decoder_callbacks_);
    decoder_callbacks_->sendLocalReply(
        Code::BadRequest, "The HTTP REST backend doesn't support request/response streaming",
        nullptr, Status::WellKnownGrpcStatus::InvalidArgument,
        absl::StrCat(RcDetails::get().grpc_transcode_failed_early, "{streaming_not_supported}"));
    return FilterHeadersStatus::StopIteration;
  }

  absl::Status status = ExtractHttpAnnotationValues(method_descriptor);
  if (!status.ok()) {
    ENVOY_STREAM_LOG(error, "Failed to extract http annotations: {}", *decoder_callbacks_,
                     status.message());
    decoder_callbacks_->sendLocalReply(
        Code::BadRequest, status.message(), nullptr, Status::WellKnownGrpcStatus::InvalidArgument,
        absl::StrCat(RcDetails::get().grpc_transcode_failed_early, "{",
                     StringUtil::replaceAllEmptySpace(MessageUtil::codeEnumToString(status.code())),
                     "}"));
    return FilterHeadersStatus::StopIteration;
  }

  is_request_http_body_ = method_descriptor->input_type()->full_name() ==
                          google::api::HttpBody::descriptor()->full_name();
  is_response_http_body_ = method_descriptor->output_type()->full_name() ==
                           google::api::HttpBody::descriptor()->full_name();

  if (!is_request_http_body_) {
    is_request_nested_http_body_ =
        per_route_config_->IsRequestNestedHttpBody(method_descriptor, http_request_body_field_);
  }

  absl::StatusOr<std::unique_ptr<Transcoder>> transcoder_or =
      per_route_config_->CreateTranscoder(method_descriptor, request_in_, response_in_);

  if (!transcoder_or.ok()) {
    ENVOY_STREAM_LOG(error, "Failed to create a transcoder instance: {}", *decoder_callbacks_,
                     transcoder_or.status().message());
    decoder_callbacks_->sendLocalReply(
        Code::BadRequest, transcoder_or.status().message(), nullptr,
        Status::WellKnownGrpcStatus::InvalidArgument,
        absl::StrCat(RcDetails::get().grpc_transcode_failed_early, "{",
                     StringUtil::replaceAllEmptySpace(
                         MessageUtil::codeEnumToString(transcoder_or.status().code())),
                     "}"));
    return FilterHeadersStatus::StopIteration;
  }
  transcoder_ = std::move(transcoder_or.value());
  request_headers_ = &headers;

  ReplaceAPIVersionInPath();

  MaybeExpandBufferLimits();

  request_content_type_ = headers.getContentTypeValue();
  headers.setContentType(Http::Headers::get().ContentTypeValues.Json);
  headers.setMethod(http_request_method_);
  headers.removeContentLength();

  decoder_callbacks_->downstreamCallbacks()->clearRouteCache();

  return FilterHeadersStatus::StopIteration;
}

FilterDataStatus GrpcJsonReverseTranscoderFilter::decodeData(Buffer::Instance& data,
                                                             bool end_stream) {
  if (!transcoder_) {
    ENVOY_STREAM_LOG(info, "Request data is passed through", *decoder_callbacks_);
    return FilterDataStatus::Continue;
  }
  if (is_request_http_body_) {
    ENVOY_STREAM_LOG(debug, "Request message is of type HttpBody", *decoder_callbacks_);
    if (absl::StrContains(http_request_path_template_, "{")) {
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
          absl::StrCat(RcDetails::get().grpc_transcode_failed, "{failed_to_create_request_path}"));
      return FilterDataStatus::StopIterationNoBuffer;
    }
    if (BuildRequestFromHttpBody(data)) {
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

  json payload = json::parse(request_buffer_.toString(), nullptr, false);
  if (payload.is_discarded()) {
    ENVOY_STREAM_LOG(error, "Failed to parse the transcoded request to build the path header.",
                     *decoder_callbacks_);
    decoder_callbacks_->sendLocalReply(
        Code::BadRequest, "Failed to parse the transcoded request", nullptr,
        Status::WellKnownGrpcStatus::InvalidArgument,
        absl::StrCat(RcDetails::get().grpc_transcode_failed, "{failed_to_parse_request_body}"));
    return FilterDataStatus::StopIterationNoBuffer;
  }

  // Check if there is a request body to be sent with the HTTP request. If yes,
  // is it the entire gRPC request message or just a field from the request
  // message.
  if (!http_request_body_field_.empty()) {
    if (http_request_body_field_ == "*") {
      ENVOY_STREAM_LOG(debug, "Using the entire gRPC message as the request body",
                       *decoder_callbacks_);
      data.move(request_buffer_);
    } else {
      Buffer::OwnedImpl buffer;
      if (!CreateDataBuffer(payload, buffer)) {
        return FilterDataStatus::StopIterationNoBuffer;
      }
      ENVOY_STREAM_LOG(debug, "Using the field, {}, from the gRPC message as the request body",
                       *decoder_callbacks_, http_request_body_field_);
      data.move(buffer);
    }
    request_headers_->setContentLength(data.length());
  }
  absl::StatusOr<std::string> path =
      BuildPath(payload, http_request_path_template_, http_request_body_field_);
  if (!path.ok()) {
    ENVOY_STREAM_LOG(error, "Failed to build the path header: {}", *decoder_callbacks_,
                     path.status().message());
    decoder_callbacks_->sendLocalReply(
        Code::BadRequest, path.status().message(), nullptr,
        Status::WellKnownGrpcStatus::InvalidArgument,
        absl::StrCat(RcDetails::get().grpc_transcode_failed, "{failed_to_build_request_path}"));
    return FilterDataStatus::StopIterationNoBuffer;
  }
  request_headers_->setPath(path.value());
  return FilterDataStatus::Continue;
}

FilterHeadersStatus GrpcJsonReverseTranscoderFilter::encodeHeaders(ResponseHeaderMap& headers,
                                                                   bool end_stream) {
  if (Grpc::Common::isGrpcResponseHeaders(headers, end_stream)) {
    ENVOY_STREAM_LOG(info, "Response type is gRPC", *encoder_callbacks_);
    is_response_passed_through_ = true;
  }
  if (!transcoder_ || is_response_passed_through_) {
    ENVOY_STREAM_LOG(info, "Response headers are passed through", *encoder_callbacks_);
    return FilterHeadersStatus::Continue;
  }
  response_content_type_ = headers.getContentTypeValue();
  headers.setContentType(request_content_type_);
  grpc_status_ = GrpcStatusFromHeaders(headers);

  // gRPC client always expect the HTTP status to be 200.
  headers.setStatus(static_cast<uint32_t>(Code::OK));
  headers.removeContentLength();

  if (end_stream) {
    Buffer::OwnedImpl buffer;
    Grpc::Encoder().prependFrameHeader(Grpc::GRPC_FH_DEFAULT, buffer, 0);
    encoder_callbacks_->addEncodedData(buffer, false);

    headers.setContentLength(Grpc::GRPC_FRAME_HEADER_SIZE);

    auto& trailers = encoder_callbacks_->addEncodedTrailers();
    trailers.setGrpcStatus(grpc_status_);

    response_in_.finish();
  }

  return FilterHeadersStatus::Continue;
}

FilterDataStatus GrpcJsonReverseTranscoderFilter::encodeData(Buffer::Instance& data,
                                                             bool end_stream) {
  if (!transcoder_ || is_response_passed_through_) {
    ENVOY_STREAM_LOG(info, "Response data is passed through", *encoder_callbacks_);
    return FilterDataStatus::Continue;
  }

  if (is_non_ok_response_) {
    ENVOY_STREAM_LOG(debug,
                     "Buffering the payload of non-200 response to be added to "
                     "the trailers later.",
                     *encoder_callbacks_);
    error_buffer_.move(data);
  } else if (is_response_http_body_) {
    response_data_.move(data);
    if (EncoderBufferLimitReached(response_data_.length())) {
      return FilterDataStatus::StopIterationNoBuffer;
    }
  } else {
    response_in_.move(data);
    if (EncoderBufferLimitReached(response_in_.bytesStored() + response_buffer_.length())) {
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
  } else if (is_response_http_body_) {
    SendHttpBodyResponse(&data);
  } else {
    data.move(response_buffer_);
  }
  return FilterDataStatus::Continue;
}

FilterTrailersStatus GrpcJsonReverseTranscoderFilter::encodeTrailers(ResponseTrailerMap& trailers) {
  if (!transcoder_ || is_response_passed_through_) {
    ENVOY_STREAM_LOG(info, "Response trailers are passed through", *encoder_callbacks_);
    return FilterTrailersStatus::Continue;
  }
  trailers.setGrpcStatus(grpc_status_);
  if (is_non_ok_response_) {
    ENVOY_STREAM_LOG(debug, "Adding the response error payload to the trailers",
                     *encoder_callbacks_);
    trailers.setGrpcMessage(BuildGrpcMessage(error_buffer_));
  } else if (is_response_http_body_) {
    SendHttpBodyResponse(nullptr);
  } else {
    encoder_callbacks_->addEncodedData(response_buffer_, false);
  }
  response_in_.finish();

  return FilterTrailersStatus::Continue;
}

} // namespace GrpcJsonReverseTranscoder
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
