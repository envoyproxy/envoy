#include "source/extensions/filters/http/proto_api_scrubber/filter.h"

#include <chrono>
#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "envoy/buffer/buffer.h"
#include "envoy/http/filter.h"
#include "envoy/http/header_map.h"
#include "envoy/matcher/matcher.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/grpc/common.h"
#include "source/common/grpc/status.h"
#include "source/common/http/codes.h"
#include "source/common/http/matching/data_impl.h"
#include "source/extensions/filters/http/grpc_field_extraction/message_converter/message_converter.h"
#include "source/extensions/filters/http/grpc_field_extraction/message_converter/message_converter_utility.h"
#include "source/extensions/filters/http/grpc_field_extraction/message_converter/stream_message.h"
#include "source/extensions/filters/http/proto_api_scrubber/filter_config.h"
#include "source/extensions/filters/http/proto_api_scrubber/scrubbing_util_lib/field_checker.h"

#include "absl/log/check.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "proto_field_extraction/message_data/cord_message_data.h"
#include "proto_processing_lib/proto_scrubber/field_checker_interface.h"
#include "proto_processing_lib/proto_scrubber/proto_scrubber.h"
#include "proto_processing_lib/proto_scrubber/proto_scrubber_enums.h"
#include "scrubbing_util_lib/field_checker.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ProtoApiScrubber {
namespace {
using ::Envoy::Extensions::HttpFilters::GrpcFieldExtraction::CreateMessageDataFunc;
using ::Envoy::Extensions::HttpFilters::GrpcFieldExtraction::MessageConverter;
using ::Envoy::Extensions::HttpFilters::GrpcFieldExtraction::StreamMessage;
using ::Envoy::Grpc::Status;
using ::Envoy::Grpc::Utility;
using ::Envoy::Http::Matching::HttpMatchingDataImpl;
using proto_processing_lib::proto_scrubber::FieldCheckerInterface;
using proto_processing_lib::proto_scrubber::ProtoScrubber;
using proto_processing_lib::proto_scrubber::ScrubberContext;

const char kRcDetailFilterProtoApiScrubber[] = "proto_api_scrubber";
const char kRcDetailErrorRequestBufferConversion[] = "REQUEST_BUFFER_CONVERSION_FAIL";
const char kRcDetailErrorResponseBufferConversion[] = "RESPONSE_BUFFER_CONVERSION_FAIL";
const char kRcDetailMethodBlocked[] = "METHOD_BLOCKED";
const char kRcDetailErrorTypeBadRequest[] = "BAD_REQUEST";
const char kPathValidationError[] = "Error in `:path` header validation.";

std::string formatError(absl::string_view filter_name, absl::string_view error_type,
                        absl::string_view error_detail) {
  return absl::StrCat(filter_name, "_", error_type, "{", error_detail, "}");
}

// Returns whether the fully qualified `api_name` is valid or not.
// Checks for separator '.' in the name and verifies each substring between these separators are
// non-empty. Note that it does not verify whether the API actually exists or not.
bool isApiNameValid(absl::string_view api_name) {
  const std::vector<absl::string_view> api_name_parts = absl::StrSplit(api_name, '.');
  if (api_name_parts.size() <= 1) {
    return false;
  }

  // Returns true if all of the api_name_parts are non-empty, otherwise returns false.
  return !std::any_of(api_name_parts.cbegin(), api_name_parts.cend(),
                      [](const absl::string_view s) { return s.empty(); });
}

// Checks if the `method_name` is a valid, fully qualified gRPC method path
// in the form "/package.ServiceName/MethodName".
// Returns absl::InvalidArgumentError if the format is incorrect or contains wildcards.
absl::Status validateMethodName(absl::string_view method_name) {
  if (method_name.empty()) {
    return absl::InvalidArgumentError(
        fmt::format("Invalid method name: '{}'. Method name is empty.", method_name));
  }

  if (absl::StrContains(method_name, '*')) {
    return absl::InvalidArgumentError(
        fmt::format("Invalid method name: '{}'. Method name contains '*' which is not supported.",
                    method_name));
  }

  const std::vector<absl::string_view> method_name_parts = absl::StrSplit(method_name, '/');
  if (method_name_parts.size() != 3 || !method_name_parts[0].empty() ||
      method_name_parts[1].empty() || !isApiNameValid(method_name_parts[1]) ||
      method_name_parts[2].empty()) {
    return absl::InvalidArgumentError(
        fmt::format("Invalid method name: '{}'. Method name should follow the gRPC format "
                    "('/package.ServiceName/MethodName').",
                    method_name));
  }

  return absl::OkStatus();
}

} // namespace

bool ProtoApiScrubberFilter::checkMethodLevelRestrictions(Envoy::Http::RequestHeaderMap& headers) {
  ENVOY_STREAM_LOG(trace, "Checking method-level restrictions for method: {}", *decoder_callbacks_,
                   method_name_);

  auto method_matcher = filter_config_.getMethodMatcher(method_name_);
  if (method_matcher == nullptr) {
    ENVOY_STREAM_LOG(trace, "No method-level restriction found for method: {}", *decoder_callbacks_,
                     method_name_);
    return false; // No specific rule, allow.
  }

  HttpMatchingDataImpl matching_data(decoder_callbacks_->streamInfo());
  matching_data.onRequestHeaders(headers);

  auto match_result = method_matcher->match(matching_data);

  // 'Envoy::Matcher::MatchResult' is the struct type, 'MatchState::UnableToMatch' is the value.
  if (match_result.isInsufficientData()) {
    ENVOY_STREAM_LOG(warn,
                     "Method-level matcher evaluation for {} was not complete. Allowing request.",
                     *decoder_callbacks_, method_name_);
    return false; // Fail open on matcher issues.
  }

  if (match_result.isMatch()) {
    ENVOY_STREAM_LOG(debug, "Method-level restriction matched for {}, blocking request.",
                     *decoder_callbacks_, method_name_);

    filter_config_.stats().method_blocked_.inc();
    decoder_callbacks_->activeSpan().setTag("proto_api_scrubber.outcome", "blocked");

    rejectRequest(Status::NotFound, "Method not allowed",
                  formatError(kRcDetailFilterProtoApiScrubber,
                              Envoy::Http::CodeUtility::toString(Http::Code::NotFound),
                              kRcDetailMethodBlocked));
    return true; // Block the request.
  }

  ENVOY_STREAM_LOG(trace, "Method-level restriction did not match for {}. Allowing headers.",
                   *decoder_callbacks_, method_name_);
  return false; // No match, allow.
}

Http::FilterHeadersStatus
ProtoApiScrubberFilter::decodeHeaders(Envoy::Http::RequestHeaderMap& headers, bool) {
  ENVOY_STREAM_LOG(trace, "Called ProtoApiScrubber Filter : {}", *decoder_callbacks_, __func__);
  filter_config_.stats().total_requests_.inc();

  if (!Envoy::Grpc::Common::isGrpcRequestHeaders(headers)) {
    ENVOY_STREAM_LOG(debug,
                     "Request isn't gRPC as its headers don't have application/grpc content-type. "
                     "Passed through the request without scrubbing.",
                     *decoder_callbacks_);
    return Envoy::Http::FilterHeadersStatus::Continue;
  }

  filter_config_.stats().total_requests_checked_.inc();
  is_valid_grpc_request_ = true;
  absl::string_view path = headers.Path()->value().getStringView();
  if (absl::Status status = validateMethodName(path); !status.ok()) {
    filter_config_.stats().invalid_method_name_.inc();
    rejectRequest(static_cast<Envoy::Grpc::Status::GrpcStatus>(status.code()), status.message(),
                  formatError(kRcDetailFilterProtoApiScrubber,
                              absl::StatusCodeToString(status.code()), kPathValidationError));
    return Envoy::Http::FilterHeadersStatus::StopIteration;
  }

  method_name_ = std::string(path);

  // Perform method-level restriction check.
  if (checkMethodLevelRestrictions(headers)) {
    return Envoy::Http::FilterHeadersStatus::StopIteration; // Request was rejected.
  }

  // If not blocked, proceed to set up for data phase.
  auto cord_message_data_factory = std::make_unique<CreateMessageDataFunc>(
      []() { return std::make_unique<Protobuf::field_extraction::CordMessageData>(); });

  request_msg_converter_ = std::make_unique<MessageConverter>(
      std::move(cord_message_data_factory), decoder_callbacks_->decoderBufferLimit());

  return Envoy::Http::FilterHeadersStatus::Continue;
}

Http::FilterDataStatus ProtoApiScrubberFilter::decodeData(Buffer::Instance& data, bool end_stream) {
  ENVOY_STREAM_LOG(debug, "Called ProtoApiScrubber::decodeData: data size={} end_stream={}",
                   *decoder_callbacks_, data.length(), end_stream);

  if (!is_valid_grpc_request_) {
    ENVOY_STREAM_LOG(debug, "Request isn't gRPC. Passed through the request without scrubbing.",
                     *decoder_callbacks_);
    return Envoy::Http::FilterDataStatus::Continue;
  }

  // Move the data to internal gRPC buffer messages representation.
  auto messages = request_msg_converter_->accumulateMessages(data, end_stream);
  if (const absl::Status& status = messages.status(); !status.ok()) {
    filter_config_.stats().request_buffer_conversion_error_.inc();

    // Correctly use the status code from the error for rejectRequest.
    Envoy::Grpc::Status::GrpcStatus grpc_status = Status::WellKnownGrpcStatus::Internal;
    if (status.code() == absl::StatusCode::kFailedPrecondition) {
      grpc_status = Status::WellKnownGrpcStatus::FailedPrecondition;
    } else if (status.code() == absl::StatusCode::kResourceExhausted) {
      grpc_status = Status::WellKnownGrpcStatus::ResourceExhausted;
    }
    rejectRequest(grpc_status, status.message(),
                  formatError(kRcDetailFilterProtoApiScrubber,
                              absl::StatusCodeToString(status.code()),
                              kRcDetailErrorRequestBufferConversion));
    return Envoy::Http::FilterDataStatus::StopIterationNoBuffer;
  }

  if (messages->empty()) {
    ENVOY_STREAM_LOG(debug, "not a complete msg", *decoder_callbacks_);
    return end_stream ? Http::FilterDataStatus::Continue
                      : Http::FilterDataStatus::StopIterationAndBuffer;
  }

  // Scrub each message individually, one by one.
  ENVOY_STREAM_LOG(trace, "Accumulated {} messages. Starting scrubbing on each of them one by one.",
                   *decoder_callbacks_, messages->size());

  // Only create the request scrubber if it's not already created.
  if (!request_scrubber_) {
    absl::StatusOr<std::unique_ptr<ProtoScrubber>> request_scrubber_or_status =
        createRequestProtoScrubber();

    if (!request_scrubber_or_status.ok()) {
      filter_config_.stats().request_scrubbing_failed_.inc();
      const absl::Status& status = request_scrubber_or_status.status();

      ENVOY_STREAM_LOG(error, "Unable to scrub request payload. Error details: {}",
                       *decoder_callbacks_, status.ToString());

      rejectRequest(static_cast<Envoy::Grpc::Status::GrpcStatus>(status.code()), status.message(),
                    formatError(kRcDetailFilterProtoApiScrubber,
                                absl::StatusCodeToString(status.code()),
                                kRcDetailErrorTypeBadRequest));

      return Envoy::Http::FilterDataStatus::StopIterationNoBuffer;
    }

    // Move the created scrubber into the MEMBER variable.
    request_scrubber_ = std::move(request_scrubber_or_status).value();
  }

  Buffer::OwnedImpl newData;
  for (size_t msg_idx = 0; msg_idx < messages->size(); ++msg_idx) {
    std::unique_ptr<StreamMessage> stream_message = std::move(messages->at(msg_idx));

    // MessageConverter uses an empty StreamMessage to denote the end.
    if (stream_message->message() == nullptr) {
      // Expect end_stream=true when the MessageConverter signals an stream end.
      ASSERT(end_stream);

      // Expect message_data->isFinalMessage()=true when the MessageConverter signals an stream end.
      ASSERT(stream_message->isFinalMessage());

      // Expect message_data is the last element in the vector when the MessageConverter signals an
      // stream end.
      ASSERT(msg_idx == messages->size() - 1);

      // Skip the empty message.
      continue;
    }

    auto start_time = filter_config_.timeSource().monotonicTime();
    auto request_scrubber_or_status = request_scrubber_->Scrub(stream_message->message());
    auto end_time = filter_config_.timeSource().monotonicTime();
    auto latency_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();
    filter_config_.stats().request_scrubbing_latency_.recordValue(latency_ms);

    if (!request_scrubber_or_status.ok()) {
      filter_config_.stats().request_scrubbing_failed_.inc();
      decoder_callbacks_->activeSpan().setTag("proto_api_scrubber.request_error",
                                              request_scrubber_or_status.ToString());

      ENVOY_STREAM_LOG(warn, "Scrubbing failed with error: {}. The request will not be modified.",
                       *decoder_callbacks_, request_scrubber_or_status.ToString());
    }

    auto buf_convert_status =
        convertMessageToBuffer(*request_msg_converter_, std::move(stream_message));

    if (!buf_convert_status.ok()) {
      const absl::Status& status = buf_convert_status.status();
      ENVOY_STREAM_LOG(error, "Failed to convert scrubbed message back to envoy buffer: {}",
                       *encoder_callbacks_, status.ToString());

      // Send a local reply if response conversion failed.
      rejectRequest(status.raw_code(), status.message(),
                    formatError(kRcDetailFilterProtoApiScrubber,
                                absl::StatusCodeToString(status.code()),
                                kRcDetailErrorRequestBufferConversion));
      return Envoy::Http::FilterDataStatus::StopIterationNoBuffer;
    }

    newData.move(*buf_convert_status.value());
  }
  data.move(newData);

  ENVOY_STREAM_LOG(trace, "Scrubbing completed successfully.", *decoder_callbacks_);
  return Envoy::Http::FilterDataStatus::Continue;
}

Http::FilterHeadersStatus
ProtoApiScrubberFilter::encodeHeaders(Envoy::Http::ResponseHeaderMap& headers, bool end_stream) {
  ENVOY_STREAM_LOG(trace, "Called ProtoApiScrubber Filter encodeHeaders", *encoder_callbacks_);

  if (!Envoy::Grpc::Common::isGrpcResponseHeaders(headers, end_stream)) {
    ENVOY_STREAM_LOG(
        debug,
        "Response headers is NOT application/grpc content-type. Response is passed through "
        "without message extraction.",
        *encoder_callbacks_);
    return Envoy::Http::FilterHeadersStatus::Continue;
  }

  auto cord_message_data_factory = std::make_unique<CreateMessageDataFunc>(
      []() { return std::make_unique<Protobuf::field_extraction::CordMessageData>(); });

  response_msg_converter_ = std::make_unique<MessageConverter>(
      std::move(cord_message_data_factory), encoder_callbacks_->encoderBufferLimit());

  return Envoy::Http::FilterHeadersStatus::Continue;
}

Http::FilterDataStatus ProtoApiScrubberFilter::encodeData(Buffer::Instance& data, bool end_stream) {
  ENVOY_STREAM_LOG(debug, "Called ProtoApiScrubber::encodeData: data size={} end_stream={}",
                   *encoder_callbacks_, data.length(), end_stream);

  if (!response_msg_converter_) {
    return Envoy::Http::FilterDataStatus::Continue;
  }

  // Move the data to internal gRPC buffer messages representation.
  auto messages = response_msg_converter_->accumulateMessages(data, end_stream);
  if (const absl::Status& status = messages.status(); !status.ok()) {
    filter_config_.stats().response_buffer_conversion_error_.inc();
    rejectResponse(status.raw_code(), status.message(),
                   formatError(kRcDetailFilterProtoApiScrubber,
                               absl::StatusCodeToString(status.code()),
                               kRcDetailErrorResponseBufferConversion));
    return Envoy::Http::FilterDataStatus::StopIterationNoBuffer;
  }

  if (messages->empty()) {
    ENVOY_STREAM_LOG(debug, "not a complete msg", *encoder_callbacks_);
    return Http::FilterDataStatus::StopIterationNoBuffer;
  }

  // Scrub each message individually, one by one.
  ENVOY_STREAM_LOG(trace, "Accumulated {} messages. Starting scrubbing on each of them one by one.",
                   *encoder_callbacks_, messages->size());

  // Only create the response scrubber if it's not already created.
  if (!response_scrubber_) {
    absl::StatusOr<std::unique_ptr<ProtoScrubber>> response_scrubber_or_status =
        createResponseProtoScrubber();
    if (!response_scrubber_or_status.ok()) {
      filter_config_.stats().response_scrubbing_failed_.inc();

      const absl::Status& status = response_scrubber_or_status.status();
      ENVOY_STREAM_LOG(error, "Unable to scrub request payload. Error details: {}",
                       *encoder_callbacks_, status.ToString());
      rejectResponse(status.raw_code(), status.message(),
                     formatError(kRcDetailFilterProtoApiScrubber,
                                 absl::StatusCodeToString(status.code()),
                                 kRcDetailErrorTypeBadRequest));
      return Envoy::Http::FilterDataStatus::StopIterationNoBuffer;
    }

    // Move the created scrubber into the member variable
    response_scrubber_ = std::move(response_scrubber_or_status).value();
  }

  for (size_t msg_idx = 0; msg_idx < messages->size(); ++msg_idx) {
    std::unique_ptr<StreamMessage> stream_message = std::move(messages->at(msg_idx));
    if (stream_message->message() == nullptr) {
      // Expect end_stream=true when the MessageConverter signals an stream end.
      ASSERT(end_stream);
      // Expect message_data->isFinalMessage()=true when the MessageConverter signals an stream end.
      ASSERT(stream_message->isFinalMessage());
      // Expect message_data is the last element in the vector when the MessageConverter signals an
      // stream end.
      ASSERT(msg_idx == messages->size() - 1);
      // Skip the empty message
      continue;
    }

    auto start_time = filter_config_.timeSource().monotonicTime();
    auto response_scrubber_or_status = response_scrubber_->Scrub(stream_message->message());
    auto end_time = filter_config_.timeSource().monotonicTime();
    auto latency_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();
    filter_config_.stats().response_scrubbing_latency_.recordValue(latency_ms);

    if (!response_scrubber_or_status.ok()) {
      filter_config_.stats().response_scrubbing_failed_.inc();
      encoder_callbacks_->activeSpan().setTag("proto_api_scrubber.response_error",
                                              response_scrubber_or_status.ToString());

      ENVOY_STREAM_LOG(warn,
                       "Response scrubbing failed with error: {}. The response will not be "
                       "modified.",
                       *encoder_callbacks_, response_scrubber_or_status.ToString());
    }

    auto buf_convert_status =
        convertMessageToBuffer(*response_msg_converter_, std::move(stream_message));

    if (!buf_convert_status.ok()) {
      filter_config_.stats().response_buffer_conversion_error_.inc();

      const absl::Status& status = buf_convert_status.status();
      ENVOY_STREAM_LOG(error, "Failed to convert scrubbed message back to envoy buffer: {}",
                       *encoder_callbacks_, status.ToString());

      // Send a local reply if response conversion failed.
      rejectResponse(status.raw_code(), status.message(),
                     formatError(kRcDetailFilterProtoApiScrubber,
                                 absl::StatusCodeToString(status.code()),
                                 kRcDetailErrorResponseBufferConversion));
      return Envoy::Http::FilterDataStatus::StopIterationNoBuffer;
    }

    data.move(*buf_convert_status.value());
  }

  ENVOY_STREAM_LOG(trace, "Response scrubbing completed successfully.", *encoder_callbacks_);
  return Envoy::Http::FilterDataStatus::Continue;
}

absl::StatusOr<std::unique_ptr<ProtoScrubber>>
ProtoApiScrubberFilter::createRequestProtoScrubber() {
  absl::StatusOr<const Protobuf::Type*> request_type_or_status =
      filter_config_.getRequestType(method_name_);

  RETURN_IF_NOT_OK(request_type_or_status.status());

  request_match_tree_field_checker_ = std::make_unique<FieldChecker>(
      ScrubberContext::kRequestScrubbing, &decoder_callbacks_->streamInfo(),
      decoder_callbacks_->requestHeaders(), OptRef<const Http::ResponseHeaderMap>{},
      decoder_callbacks_->requestTrailers(), OptRef<const Http::ResponseTrailerMap>{}, method_name_,
      &filter_config_);

  return std::make_unique<ProtoScrubber>(
      request_type_or_status.value(), filter_config_.getTypeFinder(),
      std::vector<const FieldCheckerInterface*>{request_match_tree_field_checker_.get()},
      ScrubberContext::kRequestScrubbing, false);
}

absl::StatusOr<std::unique_ptr<ProtoScrubber>>
ProtoApiScrubberFilter::createResponseProtoScrubber() {
  absl::StatusOr<const Protobuf::Type*> response_type_or_status =
      filter_config_.getResponseType(method_name_);
  RETURN_IF_NOT_OK(response_type_or_status.status());

  response_match_tree_field_checker_ = std::make_unique<FieldChecker>(
      ScrubberContext::kResponseScrubbing, &encoder_callbacks_->streamInfo(),
      decoder_callbacks_->requestHeaders(), encoder_callbacks_->responseHeaders(),
      decoder_callbacks_->requestTrailers(), encoder_callbacks_->responseTrailers(), method_name_,
      &filter_config_);

  return std::make_unique<ProtoScrubber>(
      response_type_or_status.value(), filter_config_.getTypeFinder(),
      std::vector<const FieldCheckerInterface*>{response_match_tree_field_checker_.get()},
      ScrubberContext::kResponseScrubbing, false);
}

void ProtoApiScrubberFilter::rejectRequest(Envoy::Grpc::Status::GrpcStatus grpc_status,
                                           absl::string_view error_msg,
                                           absl::string_view rc_detail) {
  ENVOY_STREAM_LOG(debug, "Rejecting request: grpcStatus={}, message={}", *decoder_callbacks_,
                   grpc_status, error_msg);
  decoder_callbacks_->sendLocalReply(
      static_cast<Envoy::Http::Code>(Utility::grpcToHttpStatus(grpc_status)), error_msg, nullptr,
      grpc_status, rc_detail);
}

void ProtoApiScrubberFilter::rejectResponse(Envoy::Grpc::Status::GrpcStatus grpc_status,
                                            absl::string_view error_msg,
                                            absl::string_view rc_detail) {
  ENVOY_STREAM_LOG(debug, "Rejecting response grpcStatus={}, message={}", *encoder_callbacks_,
                   grpc_status, error_msg);
  encoder_callbacks_->sendLocalReply(
      static_cast<Envoy::Http::Code>(Utility::grpcToHttpStatus(grpc_status)), error_msg, nullptr,
      grpc_status, rc_detail);
}

} // namespace ProtoApiScrubber
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
