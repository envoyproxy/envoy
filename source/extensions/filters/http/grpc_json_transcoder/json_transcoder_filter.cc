#include "source/extensions/filters/http/grpc_json_transcoder/json_transcoder_filter.h"

#include <memory>

#include "envoy/extensions/filters/http/grpc_json_transcoder/v3/transcoder.pb.h"
#include "envoy/http/filter.h"

#include "source/common/common/enum_to_int.h"
#include "source/common/common/utility.h"
#include "source/common/grpc/common.h"
#include "source/common/http/headers.h"
#include "source/common/http/utility.h"
#include "source/common/protobuf/protobuf.h"
#include "source/common/protobuf/utility.h"
#include "source/common/runtime/runtime_features.h"
#include "source/extensions/filters/http/grpc_json_transcoder/http_body_utils.h"

#include "google/api/httpbody.pb.h"

using Envoy::Protobuf::io::ZeroCopyInputStream;
using google::grpc::transcoding::Transcoder;
using google::grpc::transcoding::TranscoderInputStream;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace GrpcJsonTranscoder {

struct RcDetailsValues {
  // The gRPC json transcoder filter failed to transcode when processing request headers.
  // This will generally be accompanied by details about the transcoder failure.
  const std::string GrpcTranscodeFailedEarly = "early_grpc_json_transcode_failure";
  // The gRPC json transcoder filter failed to transcode when processing the request body.
  // This will generally be accompanied by details about the transcoder failure.
  const std::string GrpcTranscodeFailed = "grpc_json_transcode_failure";
};
using RcDetails = ConstSingleton<RcDetailsValues>;

namespace {

const Http::LowerCaseString& trailerHeader() {
  CONSTRUCT_ON_FIRST_USE(Http::LowerCaseString, "trailer");
}

} // namespace

JsonTranscoderFilter::JsonTranscoderFilter(const JsonTranscoderConfigConstSharedPtr& config,
                                           const GrpcJsonTranscoderFilterStatsSharedPtr& stats)
    : config_(config), stats_(stats) {}

void JsonTranscoderFilter::initPerRouteConfig() {
  const auto* route_local =
      Http::Utility::resolveMostSpecificPerFilterConfig<JsonTranscoderConfig>(decoder_callbacks_);

  per_route_config_ = route_local ? route_local : config_.get();
}

void JsonTranscoderFilter::maybeExpandBufferLimits() {
  const uint32_t max_request_size = per_route_config_->max_request_body_size_.value_or(0);
  if (max_request_size > decoder_callbacks_->bufferLimit()) {
    decoder_callbacks_->setBufferLimit(max_request_size);
  }
  const uint32_t max_response_size = per_route_config_->max_response_body_size_.value_or(0);
  if (max_response_size > encoder_callbacks_->bufferLimit()) {
    encoder_callbacks_->setBufferLimit(max_response_size);
  }
}

Http::FilterHeadersStatus JsonTranscoderFilter::decodeHeaders(Http::RequestHeaderMap& headers,
                                                              bool end_stream) {
  initPerRouteConfig();
  if (per_route_config_->disabled()) {
    ENVOY_STREAM_LOG(debug,
                     "Transcoding is disabled for the route. Request headers is passed through.",
                     *decoder_callbacks_);
    return Http::FilterHeadersStatus::Continue;
  }

  if (Grpc::Common::isGrpcRequestHeaders(headers)) {
    ENVOY_STREAM_LOG(debug,
                     "Request headers has application/grpc content-type. Request is passed through "
                     "without transcoding.",
                     *decoder_callbacks_);
    return Http::FilterHeadersStatus::Continue;
  }

  const auto status = per_route_config_->createTranscoder(headers, request_in_, response_in_,
                                                          transcoder_, method_, unknown_params_);

  if (!status.ok()) {
    ENVOY_STREAM_LOG(debug, "Failed to transcode request headers: {}", *decoder_callbacks_,
                     status.message());

    if (status.code() == absl::StatusCode::kNotFound &&
        !per_route_config_->request_validation_options_.reject_unknown_method()) {
      ENVOY_STREAM_LOG(debug,
                       "Request is passed through without transcoding because it cannot be mapped "
                       "to a gRPC method.",
                       *decoder_callbacks_);
      return Http::FilterHeadersStatus::Continue;
    }

    if (status.code() == absl::StatusCode::kInvalidArgument &&
        !per_route_config_->request_validation_options_.reject_unknown_query_parameters()) {
      ENVOY_STREAM_LOG(debug,
                       "Request is passed through without transcoding because it contains unknown "
                       "query parameters.",
                       *decoder_callbacks_);
      return Http::FilterHeadersStatus::Continue;
    }

    // protobuf::util::Status.error_code is the same as Envoy GrpcStatus
    // This cast is safe.
    auto http_code = Envoy::Grpc::Utility::grpcToHttpStatus(
        static_cast<Envoy::Grpc::Status::GrpcStatus>(status.code()));

    ENVOY_STREAM_LOG(debug, "Request is rejected due to strict rejection policy.",
                     *decoder_callbacks_);
    error_ = true;
    decoder_callbacks_->sendLocalReply(
        static_cast<Http::Code>(http_code), status.message(), nullptr, absl::nullopt,
        absl::StrCat(RcDetails::get().GrpcTranscodeFailedEarly, "{",
                     StringUtil::replaceAllEmptySpace(MessageUtil::codeEnumToString(status.code())),
                     "}"));
    return Http::FilterHeadersStatus::StopIteration;
  }

  maybeExpandBufferLimits();

  if (method_->request_type_is_http_body_) {
    if (headers.ContentType() != nullptr) {
      absl::string_view content_type = headers.getContentTypeValue();
      content_type_.assign(content_type.begin(), content_type.end());
    }

    uint64_t stream_size_before = request_in_.bytesStored();
    uint64_t buffer_size_before = initial_request_data_.length();
    bool done = !readToBuffer(*transcoder_->RequestOutput(), initial_request_data_);
    if (!done) {
      ENVOY_STREAM_LOG(
          debug,
          "Transcoding of query arguments of HttpBody request is not done (unexpected state)",
          *decoder_callbacks_);
      error_ = true;
      decoder_callbacks_->sendLocalReply(
          Http::Code::BadRequest, "Bad request", nullptr, absl::nullopt,
          absl::StrCat(RcDetails::get().GrpcTranscodeFailedEarly, "{BAD_REQUEST}"));
      return Http::FilterHeadersStatus::StopIteration;
    }
    uint64_t added = initial_request_data_.length() - buffer_size_before;
    uint64_t removed = stream_size_before - request_in_.bytesStored();
    stats_->transcoder_request_buffer_bytes_.adjust(added, removed);
    if (checkAndRejectIfRequestTranscoderFailed(RcDetails::get().GrpcTranscodeFailed)) {
      return Http::FilterHeadersStatus::StopIteration;
    }
  }

  headers.removeContentLength();
  headers.setReferenceContentType(Http::Headers::get().ContentTypeValues.Grpc);
  headers.setEnvoyOriginalPath(headers.getPathValue());
  headers.addReferenceKey(Http::Headers::get().EnvoyOriginalMethod, headers.getMethodValue());
  headers.setPath(absl::StrCat("/", method_->descriptor_->service()->full_name(), "/",
                               method_->descriptor_->name()));
  headers.setReferenceMethod(Http::Headers::get().MethodValues.Post);
  headers.setReferenceTE(Http::Headers::get().TEValues.Trailers);

  if (!per_route_config_->matchIncomingRequestInfo()) {
    decoder_callbacks_->downstreamCallbacks()->clearRouteCache();
  }

  if (end_stream && method_->request_type_is_http_body_) {
    maybeSendHttpBodyRequestMessage(nullptr);
  } else if (end_stream) {
    request_in_.finish();

    Buffer::OwnedImpl data;
    uint64_t stream_size_before = request_in_.bytesStored();
    readToBuffer(*transcoder_->RequestOutput(), data);
    stats_->transcoder_request_buffer_bytes_.sub(stream_size_before - request_in_.bytesStored());
    if (checkAndRejectIfRequestTranscoderFailed(RcDetails::get().GrpcTranscodeFailedEarly)) {
      return Http::FilterHeadersStatus::StopIteration;
    }

    if (data.length() > 0) {
      ENVOY_STREAM_LOG(debug, "adding initial data during decodeHeaders, transcoded data size={}",
                       *decoder_callbacks_, data.length());
      decoder_callbacks_->addDecodedData(data, true);
    }
  }
  return Http::FilterHeadersStatus::Continue;
}

Http::FilterDataStatus JsonTranscoderFilter::decodeData(Buffer::Instance& data, bool end_stream) {
  ASSERT(!error_);

  if (!transcoder_) {
    ENVOY_STREAM_LOG(debug, "Request data is passed through", *decoder_callbacks_);
    return Http::FilterDataStatus::Continue;
  }

  if (method_->request_type_is_http_body_) {
    stats_->transcoder_request_buffer_bytes_.add(data.length());
    request_data_.move(data);
    if (!method_->descriptor_->client_streaming() &&
        decoderBufferLimitReached(request_data_.length())) {
      return Http::FilterDataStatus::StopIterationNoBuffer;
    }

    if (method_->descriptor_->client_streaming()) {
      // To avoid sending a grpc frame larger than 4MB (which grpc will by default reject),
      // split the input buffer into 1MB pieces until the buffer is smaller than 1MB.
      Buffer::OwnedImpl remaining_request_data;
      remaining_request_data.move(request_data_);
      while (!first_request_sent_ || remaining_request_data.length() > 0) {
        uint64_t piece_size = std::min<uint64_t>(remaining_request_data.length(),
                                                 JsonTranscoderConfig::MaxStreamedPieceSize);
        request_data_.move(remaining_request_data, piece_size);
        maybeSendHttpBodyRequestMessage(&data);
      }
    } else if (end_stream) {
      maybeSendHttpBodyRequestMessage(&data);
    } else {
      // TODO(euroelessar): Avoid buffering if content length is already known.
      return Http::FilterDataStatus::StopIterationAndBuffer;
    }
  } else {
    stats_->transcoder_request_buffer_bytes_.add(data.length());
    request_in_.move(data);
    if (decoderBufferLimitReached(request_in_.bytesStored())) {
      return Http::FilterDataStatus::StopIterationNoBuffer;
    }

    if (end_stream) {
      request_in_.finish();
    }

    // Move the transcoded data to the output buffer.
    uint64_t stream_size_before = request_in_.bytesStored();
    readToBuffer(*transcoder_->RequestOutput(), data);
    stats_->transcoder_request_buffer_bytes_.sub(stream_size_before - request_in_.bytesStored());
  }

  if (checkAndRejectIfRequestTranscoderFailed(RcDetails::get().GrpcTranscodeFailed)) {
    return Http::FilterDataStatus::StopIterationNoBuffer;
  }

  ENVOY_STREAM_LOG(debug,
                   "continuing request during decodeData, transcoded data size={}, end_stream={}",
                   *decoder_callbacks_, data.length(), end_stream);
  return Http::FilterDataStatus::Continue;
}

Http::FilterTrailersStatus JsonTranscoderFilter::decodeTrailers(Http::RequestTrailerMap&) {
  ASSERT(!error_);

  if (!transcoder_) {
    ENVOY_STREAM_LOG(debug, "Request trailers is passed through", *decoder_callbacks_);
    return Http::FilterTrailersStatus::Continue;
  }

  if (method_->request_type_is_http_body_) {
    maybeSendHttpBodyRequestMessage(nullptr);
  } else {
    request_in_.finish();

    Buffer::OwnedImpl data;
    uint64_t stream_size_before = request_in_.bytesStored();
    readToBuffer(*transcoder_->RequestOutput(), data);
    stats_->transcoder_request_buffer_bytes_.sub(stream_size_before - request_in_.bytesStored());

    if (data.length()) {
      ENVOY_STREAM_LOG(debug,
                       "adding remaining data during decodeTrailers, transcoded data size={}",
                       *decoder_callbacks_, data.length());
      decoder_callbacks_->addDecodedData(data, true);
    }
  }
  return Http::FilterTrailersStatus::Continue;
}

void JsonTranscoderFilter::setDecoderFilterCallbacks(
    Http::StreamDecoderFilterCallbacks& callbacks) {
  decoder_callbacks_ = &callbacks;
}

Http::FilterHeadersStatus JsonTranscoderFilter::encodeHeaders(Http::ResponseHeaderMap& headers,
                                                              bool end_stream) {
  if (error_ || !transcoder_) {
    ENVOY_STREAM_LOG(debug, "Response headers is passed through", *encoder_callbacks_);
    return Http::FilterHeadersStatus::Continue;
  }

  if (!Grpc::Common::isGrpcResponseHeaders(headers, end_stream)) {
    ENVOY_STREAM_LOG(
        debug,
        "Response headers is NOT application/grpc content-type. Response is passed through "
        "without transcoding.",
        *encoder_callbacks_);
    error_ = true;
    return Http::FilterHeadersStatus::Continue;
  }

  response_headers_ = &headers;

  if (end_stream) {
    if (method_->descriptor_->server_streaming()) {
      if (per_route_config_->isStreamSSEStyleDelimited()) {
        headers.setContentType(Http::Headers::get().ContentTypeValues.TextEventStream);
      } else {
        headers.setReferenceContentType(Http::Headers::get().ContentTypeValues.Json);
      }
    }

    // In gRPC wire protocol, headers frame with end_stream is a trailers-only response.
    // The return value from encodeTrailers is ignored since it is always continue.
    doTrailers(headers);

    return Http::FilterHeadersStatus::Continue;
  }

  if (method_->descriptor_->server_streaming() && per_route_config_->isStreamSSEStyleDelimited()) {
    headers.setContentType(Http::Headers::get().ContentTypeValues.TextEventStream);
  } else {
    headers.setReferenceContentType(Http::Headers::get().ContentTypeValues.Json);
  }

  // In case of HttpBody in response - content type is unknown at this moment.
  // So "Continue" only for regular streaming use case and StopIteration for
  // all other cases (non streaming, streaming + httpBody)
  if (method_->descriptor_->server_streaming() && !method_->response_type_is_http_body_) {
    return Http::FilterHeadersStatus::Continue;
  }
  return Http::FilterHeadersStatus::StopIteration;
}

Http::FilterDataStatus JsonTranscoderFilter::encodeData(Buffer::Instance& data, bool end_stream) {
  if (error_ || !transcoder_) {
    ENVOY_STREAM_LOG(debug, "Response data is passed through", *encoder_callbacks_);
    return Http::FilterDataStatus::Continue;
  }

  has_body_ = true;

  if (method_->response_type_is_http_body_) {
    bool frame_processed = buildResponseFromHttpBodyOutput(*response_headers_, data);
    if (!method_->descriptor_->server_streaming()) {
      return Http::FilterDataStatus::StopIterationAndBuffer;
    }
    if (!http_body_response_headers_set_ && !frame_processed) {
      return Http::FilterDataStatus::StopIterationAndBuffer;
    }
    return Http::FilterDataStatus::Continue;
  }

  stats_->transcoder_response_buffer_bytes_.add(data.length());
  response_in_.move(data);
  if (encoderBufferLimitReached(response_in_.bytesStored() + response_data_.length())) {
    return Http::FilterDataStatus::StopIterationNoBuffer;
  }

  if (end_stream) {
    response_in_.finish();
  }

  uint64_t stream_size_before = response_in_.bytesStored();
  uint64_t buffer_size_before = response_data_.length();
  readToBuffer(*transcoder_->ResponseOutput(), response_data_);
  uint64_t added = response_data_.length() - buffer_size_before;
  uint64_t removed = stream_size_before - response_in_.bytesStored();
  stats_->transcoder_response_buffer_bytes_.adjust(added, removed);

  if (checkAndRejectIfResponseTranscoderFailed()) {
    return Http::FilterDataStatus::StopIterationNoBuffer;
  }

  if (!method_->descriptor_->server_streaming() && !end_stream) {
    ENVOY_STREAM_LOG(debug,
                     "internally buffering unary response waiting for end_stream during "
                     "encodeData, transcoded data size={}",
                     *encoder_callbacks_, response_data_.length());
    return Http::FilterDataStatus::StopIterationNoBuffer;
  }

  data.move(response_data_);
  ENVOY_STREAM_LOG(debug,
                   "continuing response during encodeData, transcoded data size={}, end_stream={}",
                   *encoder_callbacks_, data.length(), end_stream);
  return Http::FilterDataStatus::Continue;
}

Http::FilterTrailersStatus
JsonTranscoderFilter::encodeTrailers(Http::ResponseTrailerMap& trailers) {
  doTrailers(trailers);

  return Http::FilterTrailersStatus::Continue;
}

void JsonTranscoderFilter::doTrailers(Http::ResponseHeaderOrTrailerMap& headers_or_trailers) {
  if (error_ || !transcoder_ || !per_route_config_ || per_route_config_->disabled()) {
    ENVOY_STREAM_LOG(debug, "Response headers/trailers is passed through", *encoder_callbacks_);
    return;
  }

  response_in_.finish();

  const absl::optional<Grpc::Status::GrpcStatus> grpc_status =
      Grpc::Common::getGrpcStatus(headers_or_trailers, true);
  if (grpc_status && maybeConvertGrpcStatus(*grpc_status, headers_or_trailers)) {
    return;
  }

  if (!method_->response_type_is_http_body_) {
    uint64_t stream_size_before = response_in_.bytesStored();
    uint64_t buffer_size_before = response_data_.length();
    readToBuffer(*transcoder_->ResponseOutput(), response_data_);
    stats_->transcoder_response_buffer_bytes_.add(response_data_.length() - buffer_size_before);
    stats_->transcoder_response_buffer_bytes_.sub(stream_size_before - response_in_.bytesStored());
    if (checkAndRejectIfResponseTranscoderFailed()) {
      return;
    }
    if (response_data_.length() > 0) {
      ENVOY_STREAM_LOG(debug,
                       "adding remaining data during encodeTrailers, transcoded data size={}",
                       *encoder_callbacks_, response_data_.length());
      encoder_callbacks_->addEncodedData(response_data_, true);
    }
  }

  // If there was no previous headers frame, this |trailers| map is our |response_headers_|,
  // so there is no need to copy headers from one to the other.
  const bool is_trailers_only_response = response_headers_ == &headers_or_trailers;
  const bool is_server_streaming = method_->descriptor_->server_streaming();

  if (is_server_streaming && !is_trailers_only_response) {
    // Continue if headers were sent already.
    return;
  }

  if (!grpc_status || grpc_status.value() == Grpc::Status::WellKnownGrpcStatus::InvalidCode) {
    response_headers_->setStatus(enumToInt(Http::Code::ServiceUnavailable));
  } else {
    response_headers_->setStatus(Grpc::Utility::grpcToHttpStatus(grpc_status.value()));
    if (!is_trailers_only_response) {
      response_headers_->setGrpcStatus(grpc_status.value());
    }
  }

  if (!is_trailers_only_response) {
    // Copy the grpc-message header if it exists.
    const Http::HeaderEntry* grpc_message_header = headers_or_trailers.GrpcMessage();
    if (grpc_message_header) {
      response_headers_->setGrpcMessage(grpc_message_header->value().getStringView());
    }
  }

  // remove Trailer headers if the client connection was http/1
  if (encoder_callbacks_->streamInfo().protocol() < Http::Protocol::Http2) {
    response_headers_->remove(trailerHeader());
  }

  if (!method_->descriptor_->server_streaming()) {
    // Set content-length for non-streaming responses.
    response_headers_->setContentLength(
        encoder_callbacks_->encodingBuffer() ? encoder_callbacks_->encodingBuffer()->length() : 0);
  }
}

void JsonTranscoderFilter::setEncoderFilterCallbacks(
    Http::StreamEncoderFilterCallbacks& callbacks) {
  encoder_callbacks_ = &callbacks;
}

bool JsonTranscoderFilter::checkAndRejectIfRequestTranscoderFailed(const std::string& details) {
  const auto& request_status = transcoder_->RequestStatus();
  if (!request_status.ok()) {
    ENVOY_STREAM_LOG(debug, "Transcoding request error {}", *decoder_callbacks_,
                     request_status.ToString());
    error_ = true;
    decoder_callbacks_->sendLocalReply(
        Http::Code::BadRequest,
        absl::string_view(request_status.message().data(), request_status.message().size()),
        nullptr, absl::nullopt,
        absl::StrCat(
            details, "{",
            StringUtil::replaceAllEmptySpace(MessageUtil::codeEnumToString(request_status.code())),
            "}"));

    return true;
  }
  return false;
}

bool JsonTranscoderFilter::checkAndRejectIfResponseTranscoderFailed() {
  const auto& response_status = transcoder_->ResponseStatus();
  if (!response_status.ok()) {
    ENVOY_STREAM_LOG(debug, "Transcoding response error {}", *encoder_callbacks_,
                     response_status.ToString());
    error_ = true;
    encoder_callbacks_->sendLocalReply(
        Http::Code::BadGateway,
        absl::string_view(response_status.message().data(), response_status.message().size()),
        nullptr, absl::nullopt,
        absl::StrCat(
            RcDetails::get().GrpcTranscodeFailed, "{",
            StringUtil::replaceAllEmptySpace(MessageUtil::codeEnumToString(response_status.code())),
            "}"));

    return true;
  }
  return false;
}

bool JsonTranscoderFilter::readToBuffer(Protobuf::io::ZeroCopyInputStream& stream,
                                        Buffer::Instance& data) {
  const void* out;
  int size;
  while (stream.Next(&out, &size)) {
    if (size == 0) {
      return true;
    }
    data.add(out, size);
  }
  return false;
}

void JsonTranscoderFilter::onDestroy() {
  if (request_data_.length() || request_in_.bytesStored()) {
    stats_->transcoder_request_buffer_bytes_.sub(request_data_.length() +
                                                 request_in_.bytesStored());
  }
  if (response_data_.length() || response_in_.bytesStored()) {
    stats_->transcoder_response_buffer_bytes_.sub(response_data_.length() +
                                                  response_in_.bytesStored());
  }
}

void JsonTranscoderFilter::maybeSendHttpBodyRequestMessage(Buffer::Instance* data) {
  if (first_request_sent_ && request_data_.length() == 0) {
    return;
  }

  Buffer::OwnedImpl message_payload;
  stats_->transcoder_request_buffer_bytes_.sub(initial_request_data_.length());
  message_payload.move(initial_request_data_);
  HttpBodyUtils::appendHttpBodyEnvelope(message_payload, method_->request_body_field_path,
                                        std::move(content_type_), request_data_.length(),
                                        unknown_params_);
  content_type_.clear();
  stats_->transcoder_request_buffer_bytes_.sub(request_data_.length());
  message_payload.move(request_data_);

  Envoy::Grpc::Encoder().prependFrameHeader(Envoy::Grpc::GRPC_FH_DEFAULT, message_payload);

  if (data) {
    data->move(message_payload);
  } else {
    decoder_callbacks_->addDecodedData(message_payload, true);
  }

  first_request_sent_ = true;
}

bool JsonTranscoderFilter::buildResponseFromHttpBodyOutput(
    Http::ResponseHeaderMap& response_headers, Buffer::Instance& data) {
  std::vector<Grpc::Frame> frames;
  std::ignore = decoder_.decode(data, frames);
  if (frames.empty()) {
    return false;
  }

  google::api::HttpBody http_body;
  for (auto& frame : frames) {
    if (frame.length_ > 0) {
      http_body.Clear();
      Buffer::ZeroCopyInputStreamImpl stream(std::move(frame.data_));
      if (!HttpBodyUtils::parseMessageByFieldPath(&stream, method_->response_body_field_path,
                                                  &http_body)) {
        // TODO(euroelessar): Return error to client.
        encoder_callbacks_->resetStream();
        return true;
      }
      const auto& body = MessageUtil::bytesToString(http_body.data());

      data.add(body);

      if (!method_->descriptor_->server_streaming()) {
        // Non streaming case: single message with content type / length
        response_headers.setContentType(http_body.content_type());
        response_headers.setContentLength(body.size());
        return true;
      } else if (!http_body_response_headers_set_) {
        // Streaming case: set content type only once from first HttpBody message
        response_headers.setContentType(http_body.content_type());
        http_body_response_headers_set_ = true;
      }
    }
  }

  return true;
}

bool JsonTranscoderFilter::maybeConvertGrpcStatus(Grpc::Status::GrpcStatus grpc_status,
                                                  Http::ResponseHeaderOrTrailerMap& trailers) {
  ASSERT(per_route_config_ && !per_route_config_->disabled());
  if (!per_route_config_->convertGrpcStatus()) {
    return false;
  }

  // Send a serialized status only if there was no body.
  if (has_body_) {
    return false;
  }

  if (grpc_status == Grpc::Status::WellKnownGrpcStatus::Ok ||
      grpc_status == Grpc::Status::WellKnownGrpcStatus::InvalidCode) {
    return false;
  }

  // TODO(mattklein123): The dynamic cast here is needed because ResponseHeaderOrTrailerMap is not
  // a header map. This can likely be cleaned up.
  auto status_details =
      Grpc::Common::getGrpcStatusDetailsBin(dynamic_cast<Http::HeaderMap&>(trailers));
  if (!status_details) {
    // If no rpc.Status object was sent in the grpc-status-details-bin header,
    // construct it from the grpc-status and grpc-message headers.
    status_details.emplace();
    status_details->set_code(grpc_status);

    auto grpc_message_header = trailers.GrpcMessage();
    if (grpc_message_header) {
      auto message = grpc_message_header->value().getStringView();
      auto decoded_message = Http::Utility::PercentEncoding::decode(message);
      status_details->set_message(decoded_message.data(), decoded_message.size());
    }
  }

  std::string json_status;
  auto translate_status =
      per_route_config_->translateProtoMessageToJson(*status_details, &json_status);
  if (!translate_status.ok()) {
    ENVOY_STREAM_LOG(debug, "Transcoding status error {}", *encoder_callbacks_,
                     translate_status.ToString());
    return false;
  }

  response_headers_->setStatus(Grpc::Utility::grpcToHttpStatus(grpc_status));

  bool is_trailers_only_response = response_headers_ == &trailers;
  if (is_trailers_only_response) {
    // Drop the gRPC status headers, we already have them in the JSON body.
    response_headers_->removeGrpcStatus();
    response_headers_->removeGrpcMessage();
    response_headers_->remove(Http::Headers::get().GrpcStatusDetailsBin);
  }

  // remove Trailer headers if the client connection was http/1
  if (encoder_callbacks_->streamInfo().protocol() < Http::Protocol::Http2) {
    response_headers_->remove(trailerHeader());
  }

  response_headers_->setReferenceContentType(Http::Headers::get().ContentTypeValues.Json);

  response_headers_->setContentLength(json_status.length());

  Buffer::OwnedImpl status_data(json_status);
  encoder_callbacks_->addEncodedData(status_data, false);
  return true;
}

bool JsonTranscoderFilter::decoderBufferLimitReached(uint64_t buffer_length) {
  // The limit is either the configured maximum request body size, or, if not configured,
  // the default buffer limit.
  const uint32_t max_size =
      per_route_config_->max_request_body_size_.value_or(decoder_callbacks_->bufferLimit());
  if (buffer_length > max_size) {
    ENVOY_STREAM_LOG(debug,
                     "Request rejected because the transcoder's internal buffer size exceeds the "
                     "configured limit: {} > {}",
                     *decoder_callbacks_, buffer_length, max_size);
    error_ = true;
    decoder_callbacks_->sendLocalReply(
        Http::Code::PayloadTooLarge,
        "Request rejected because the transcoder's internal buffer size exceeds the configured "
        "limit.",
        nullptr, absl::nullopt,
        absl::StrCat(RcDetails::get().GrpcTranscodeFailed, "{request_buffer_size_limit_reached}"));
    return true;
  }
  return false;
}

bool JsonTranscoderFilter::encoderBufferLimitReached(uint64_t buffer_length) {
  // The limit is either the configured maximum response body size, or, if not configured,
  // the default buffer limit.
  const uint32_t max_size =
      per_route_config_->max_response_body_size_.value_or(encoder_callbacks_->bufferLimit());
  if (buffer_length > max_size) {
    ENVOY_STREAM_LOG(
        debug,
        "Response not transcoded because the transcoder's internal buffer size exceeds the "
        "configured limit: {} > {}",
        *encoder_callbacks_, buffer_length, max_size);
    error_ = true;
    encoder_callbacks_->sendLocalReply(
        Http::Code::InternalServerError,
        "Response not transcoded because the transcoder's internal buffer size exceeds the "
        "configured limit.",
        nullptr, absl::nullopt,
        absl::StrCat(RcDetails::get().GrpcTranscodeFailed, "{response_buffer_size_limit_reached}"));
    return true;
  }
  return false;
}

} // namespace GrpcJsonTranscoder
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
