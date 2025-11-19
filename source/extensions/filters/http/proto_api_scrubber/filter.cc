#include "source/extensions/filters/http/proto_api_scrubber/filter.h"

#include "envoy/http/filter.h"
#include "envoy/http/header_map.h"

#include "source/common/grpc/common.h"
#include "source/common/grpc/status.h"
#include "source/extensions/filters/http/grpc_field_extraction/message_converter/message_converter.h"
#include "source/extensions/filters/http/grpc_field_extraction/message_converter/message_converter_utility.h"
#include "source/extensions/filters/http/grpc_field_extraction/message_converter/stream_message.h"

#include "absl/log/check.h"
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
using proto_processing_lib::proto_scrubber::FieldCheckerInterface;
using proto_processing_lib::proto_scrubber::ProtoScrubber;
using proto_processing_lib::proto_scrubber::ScrubberContext;

const char kRcDetailFilterProtoApiScrubber[] = "proto_api_scrubber";
const char kRcDetailErrorRequestBufferConversion[] = "REQUEST_BUFFER_CONVERSION_FAIL";
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

Http::FilterHeadersStatus
ProtoApiScrubberFilter::decodeHeaders(Envoy::Http::RequestHeaderMap& headers, bool) {
  ENVOY_STREAM_LOG(trace, "Called ProtoApiScrubber Filter : {}", *decoder_callbacks_, __func__);

  if (!Envoy::Grpc::Common::isGrpcRequestHeaders(headers)) {
    ENVOY_STREAM_LOG(debug,
                     "Request isn't gRPC as its headers don't have application/grpc content-type. "
                     "Passed through the request without scrubbing.",
                     *decoder_callbacks_);
    return Envoy::Http::FilterHeadersStatus::Continue;
  }

  is_valid_grpc_request_ = true;
  auto path_header = headers.Path()->value().getStringView();
  if (absl::Status status = validateMethodName(path_header); !status.ok()) {
    rejectRequest(status.raw_code(), status.message(),
                  formatError(kRcDetailFilterProtoApiScrubber,
                              absl::StatusCodeToString(status.code()), kPathValidationError));
    return Envoy::Http::FilterHeadersStatus::StopIteration;
  }

  method_name_ = std::string(path_header);

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
    rejectRequest(status.raw_code(), status.message(),
                  formatError(kRcDetailFilterProtoApiScrubber,
                              absl::StatusCodeToString(status.code()),
                              kRcDetailErrorRequestBufferConversion));
    return Envoy::Http::FilterDataStatus::StopIterationNoBuffer;
  }

  if (messages->empty()) {
    ENVOY_STREAM_LOG(debug, "not a complete msg", *decoder_callbacks_);
    return Http::FilterDataStatus::StopIterationNoBuffer;
  }

  // Scrub each message individually, one by one.
  ENVOY_STREAM_LOG(trace, "Accumulated {} messages. Starting scrubbing on each of them one by one.",
                   *decoder_callbacks_, messages->size());

  // Only create the request scrubber if it's not already created.
  if (!request_scrubber_) {
    absl::StatusOr<std::unique_ptr<ProtoScrubber>> request_scrubber_or_status =
        createRequestProtoScrubber();

    if (!request_scrubber_or_status.ok()) {
      const absl::Status& status = request_scrubber_or_status.status();

      ENVOY_STREAM_LOG(error, "Unable to scrub request payload. Error details: {}",
                       *decoder_callbacks_, status.ToString());

      rejectRequest(status.raw_code(), status.message(),
                    formatError(kRcDetailFilterProtoApiScrubber,
                                absl::StatusCodeToString(status.code()),
                                kRcDetailErrorTypeBadRequest));

      return Envoy::Http::FilterDataStatus::StopIterationNoBuffer;
    }

    // Move the created scrubber into the MEMBER variable
    request_scrubber_ = std::move(request_scrubber_or_status).value();
  }

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

      // Skip the empty message
      continue;
    }

    auto status = request_scrubber_->Scrub(stream_message->message());
    if (!status.ok()) {
      ENVOY_STREAM_LOG(warn, "Scrubbing failed with error: {}. The request will not be modified.",
                       *decoder_callbacks_, status.ToString());
    }

    auto buf_convert_status =
        request_msg_converter_->convertBackToBuffer(std::move(stream_message));
    RELEASE_ASSERT(buf_convert_status.ok(), "failed to convert message back to envoy buffer");

    data.move(*buf_convert_status.value());
  }

  ENVOY_STREAM_LOG(trace, "Scrubbing completed successfully.", *decoder_callbacks_);
  return Envoy::Http::FilterDataStatus::Continue;
}

absl::StatusOr<std::unique_ptr<ProtoScrubber>>
ProtoApiScrubberFilter::createRequestProtoScrubber() {
  absl::StatusOr<const Protobuf::Type*> request_type_or_status =
      filter_config_.getRequestType(method_name_);

  RETURN_IF_NOT_OK(request_type_or_status.status());

  request_match_tree_field_checker_ = std::make_unique<FieldChecker>(
      ScrubberContext::kRequestScrubbing, &decoder_callbacks_->streamInfo(), method_name_,
      &filter_config_);

  return std::make_unique<ProtoScrubber>(
      request_type_or_status.value(), filter_config_.getTypeFinder(),
      std::vector<const FieldCheckerInterface*>{request_match_tree_field_checker_.get()},
      ScrubberContext::kRequestScrubbing, false);
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

} // namespace ProtoApiScrubber
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
