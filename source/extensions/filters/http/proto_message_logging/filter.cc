#include "source/extensions/filters/http/proto_message_logging/filter.h"

#include <algorithm>
#include <cstddef>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/log/check.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "envoy/http/filter.h"
#include "envoy/http/header_map.h"
#include "envoy/registry/registry.h"
#include "envoy/stream_info/filter_state.h"
#include "proto_field_extraction/message_data/cord_message_data.h"
#include "source/common/buffer/zero_copy_input_stream_impl.h"
#include "source/common/common/assert.h"
#include "source/common/common/base64.h"
#include "source/common/common/empty_string.h"
#include "source/common/common/logger.h"
#include "source/common/grpc/common.h"
#include "source/common/grpc/status.h"
#include "source/common/http/codes.h"
#include "source/common/http/header_map_impl.h"
#include "source/common/http/header_utility.h"
#include "source/common/http/headers.h"
#include "source/common/http/utility.h"
#include "source/common/protobuf/protobuf.h"
#include "source/extensions/filters/http/grpc_field_extraction/message_converter/message_converter.h"
#include "source/extensions/filters/http/grpc_field_extraction/message_converter/message_converter_utility.h"
#include "source/extensions/filters/http/grpc_field_extraction/message_converter/stream_message.h"
#include "source/extensions/filters/http/proto_message_logging/extractor.h"
#include "source/extensions/filters/http/proto_message_logging/logging_util/proto_scrubber_interface.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ProtoMessageLogging {
namespace {

using ::Envoy::Extensions::HttpFilters::GrpcFieldExtraction::
    CreateMessageDataFunc;
using ::Envoy::Extensions::HttpFilters::GrpcFieldExtraction::MessageConverter;
using ::Envoy::Extensions::HttpFilters::GrpcFieldExtraction::StreamMessage;
using ::Envoy::Grpc::Status;
using ::Envoy::Grpc::Utility;

// The filter prefixes.
const char kRcDetailFilterProtoMessageLogging[] = "proto_message_logging";

const char kRcDetailErrorTypeBadRequest[] = "BAD_REQUEST";

const char kRcDetailErrorRequestProtoMessageLoggingFailed[] =
    "REQUEST_PROTO_MESSAGE_LOGGING_FAILED";

const char kRcDetailErrorRequestOutOfData[] = "REQUEST_OUT_OF_DATA";

const char kRcDetailErrorResponseOutOfData[] = "RESPONSE_OUT_OF_DATA";

const char kRcDetailErrorRequestBufferConversion[] =
    "REQUEST_BUFFER_CONVERSION_FAIL";

const char kRcDetailErrorResponseBufferConversion[] =
    "RESPONSE_BUFFER_CONVERSION_FAIL";

// Turns a '/package.service/method' to 'package.service.method' which is
// the form suitable for the proto db lookup.
absl::StatusOr<std::string> grpcPathToProtoPath(absl::string_view grpc_path) {
  if (grpc_path.empty() || grpc_path.at(0) != '/' ||
      std::count(grpc_path.begin(), grpc_path.end(), '/') != 2) {
    return absl::InvalidArgumentError(absl::StrFormat(
        ":path `%s` should be in form of `/package.service/method`",
        grpc_path));
  }

  std::string clean_input = std::string(grpc_path.substr(1));
  std::replace(clean_input.begin(), clean_input.end(), '/', '.');
  return clean_input;
}

std::string generateRcDetails(absl::string_view filter_name,
                              absl::string_view error_type,
                              absl::string_view error_detail) {
  return absl::StrCat(filter_name, "_", error_type, "{", error_detail, "}");
}
}  // namespace

void Filter::rejectRequest(Envoy::Grpc::Status::GrpcStatus grpc_status,
                           absl::string_view error_msg,
                           absl::string_view rc_detail) {
  ENVOY_STREAM_LOG(debug, "Rejecting request: grpcStatus={}, message={}",
                   *decoder_callbacks_, grpc_status, error_msg);
  decoder_callbacks_->sendLocalReply(
      static_cast<Envoy::Http::Code>(Utility::grpcToHttpStatus(grpc_status)),
      error_msg, nullptr, grpc_status, rc_detail);
}

void Filter::rejectResponse(Status::GrpcStatus grpc_status,
                            absl::string_view error_msg,
                            absl::string_view rc_detail) {
  ENVOY_STREAM_LOG(debug, "Rejecting response grpcStatus={}, message={}",
                   *encoder_callbacks_, grpc_status, error_msg);

  encoder_callbacks_->sendLocalReply(
      static_cast<Envoy::Http::Code>(Utility::grpcToHttpStatus(grpc_status)),
      error_msg, nullptr, grpc_status, rc_detail);

  encoder_callbacks_->streamInfo().setResponseFlag(
      Envoy::StreamInfo::CoreResponseFlag::UnauthorizedExternalService);
}

Envoy::Http::FilterHeadersStatus Filter::decodeHeaders(
    Envoy::Http::RequestHeaderMap& headers, bool) {
  ENVOY_STREAM_LOG(debug, "In decodeHeaders", *decoder_callbacks_);

  if (!Envoy::Grpc::Common::isGrpcRequestHeaders(headers)) {
    ENVOY_STREAM_LOG(debug,
                     "Request isn't gRPC as its headers don't have "
                     "application/grpc content-type. "
                     "Passed through the request "
                     "without extraction.",
                     *decoder_callbacks_);
    return Envoy::Http::FilterHeadersStatus::Continue;
  }
  // Grpc::Common::isGrpcRequestHeaders above already ensures the existence of
  // ":path" header.
  auto proto_path =
      grpcPathToProtoPath(headers.Path()->value().getStringView());
  if (!proto_path.ok()) {
    ENVOY_STREAM_LOG(info, "failed to convert gRPC path to protobuf path: {}",
                     *decoder_callbacks_, proto_path.status().ToString());

    auto& status = proto_path.status();
    rejectRequest(status.raw_code(), status.message(),
                  generateRcDetails(kRcDetailFilterProtoMessageLogging,
                                    absl::StatusCodeToString(status.code()),
                                    kRcDetailErrorTypeBadRequest));
    return Envoy::Http::FilterHeadersStatus::StopIteration;
  }

  const auto* extractor = filter_config_.findExtractor(*proto_path);
  if (!extractor) {
    ENVOY_STREAM_LOG(
        debug,
        "gRPC method `{}` isn't configured for proto message logging logging",
        *decoder_callbacks_, *proto_path);
    return Envoy::Http::FilterHeadersStatus::Continue;
  }

  // Cast away const for necessary modifications in a controlled context.
  extractor_ = const_cast<Extractor*>(extractor);
  auto cord_message_data_factory =
      std::make_unique<CreateMessageDataFunc>([]() {
        return std::make_unique<
            google::protobuf::field_extraction::CordMessageData>();
      });

  request_msg_converter_ = std::make_unique<MessageConverter>(
      std::move(cord_message_data_factory),
      decoder_callbacks_->decoderBufferLimit());

  return Envoy::Http::FilterHeadersStatus::StopIteration;
}

Envoy::Http::FilterDataStatus Filter::decodeData(Envoy::Buffer::Instance& data,
                                                 bool end_stream) {
  ENVOY_STREAM_LOG(debug, "decodeData: data size={} end_stream={}",
                   *decoder_callbacks_, data.length(), end_stream);

  if (!extractor_ || request_logging_done_) {
    return Envoy::Http::FilterDataStatus::Continue;
  }

  if (auto status = handleDecodeData(data, end_stream); !status.got_messages) {
    return status.filter_status;
  }

  return Envoy::Http::FilterDataStatus::Continue;
}

Filter::HandleDecodeDataStatus Filter::handleDecodeData(
    Envoy::Buffer::Instance& data, bool end_stream) {
  RELEASE_ASSERT(extractor_ && request_msg_converter_,
                 "`extractor_` and request_msg_converter_ both should be "
                 "initiated when logging proto messages");

  auto buffering = request_msg_converter_->accumulateMessages(data, end_stream);
  if (!buffering.ok()) {
    const absl::Status& status = buffering.status();
    rejectRequest(status.raw_code(), status.message(),
                  generateRcDetails(kRcDetailFilterProtoMessageLogging,
                                    absl::StatusCodeToString(status.code()),
                                    kRcDetailErrorRequestBufferConversion));
    return HandleDecodeDataStatus(
        Envoy::Http::FilterDataStatus::StopIterationNoBuffer);
  }

  if (buffering->empty()) {
    ENVOY_STREAM_LOG(debug, "not a complete msg", *decoder_callbacks_);
    // Not a complete message.
    return HandleDecodeDataStatus(
        Envoy::Http::FilterDataStatus::StopIterationNoBuffer);
  }

  // Buffering returns a list of messages.
  for (size_t msg_idx = 0; msg_idx < buffering->size(); ++msg_idx) {
    std::unique_ptr<StreamMessage> message_data =
        std::move(buffering->at(msg_idx));

    // MessageConverter uses an empty StreamMessage to denote the end.
    if (message_data->message() == nullptr) {
      RELEASE_ASSERT(end_stream,
                     "expect end_stream=true as when the MessageConverter "
                     "signals an stream end");
      RELEASE_ASSERT(message_data->isFinalMessage(),
                     "expect message_data->isFinalMessage()=true when the "
                     "MessageConverter signals "
                     "an stream end");
      // This is the last one in the vector.
      RELEASE_ASSERT(
          msg_idx == buffering->size() - 1,
          "expect message_data is the last element in the vector when the "
          "MessageConverter signals an stream end");
      // Skip the empty message
      continue;
    }

    if (!request_logging_done_) {
      request_logging_done_ = true;

      extractor_->processRequest(*message_data->message());

      AuditResult result = extractor_->GetResult();

      if (result.request_data.empty()) {
        rejectRequest(
            Status::WellKnownGrpcStatus::InvalidArgument,
            "did not receive enough data for request logging.",
            generateRcDetails(
                kRcDetailFilterProtoMessageLogging,
                absl::StatusCodeToString(absl::StatusCode::kInvalidArgument),
                kRcDetailErrorRequestProtoMessageLoggingFailed));
        return HandleDecodeDataStatus(
            Envoy::Http::FilterDataStatus::StopIterationNoBuffer);
      }
      handleLoggingResult(result.request_data, MetadataType::RQUEST);
    }

    auto buf_convert_status =
        request_msg_converter_->convertBackToBuffer(std::move(message_data));
    // The message_data is not modified, ConvertBack should return OK.
    RELEASE_ASSERT(buf_convert_status.ok(),
                   "request message convert back should work");

    data.move(*buf_convert_status.value());
    ENVOY_STREAM_LOG(debug, "decodeData: convert back data size={}",
                     *decoder_callbacks_, data.length());
  }

  // Reject the request if  extraction is required but could not
  // buffer up any messages.
  if (!request_logging_done_) {
    rejectRequest(Status::WellKnownGrpcStatus::InvalidArgument,
                  "did not receive enough data to form a message.",
                  generateRcDetails(kRcDetailFilterProtoMessageLogging,
                                    absl::StatusCodeToString(
                                        absl::StatusCode::kInvalidArgument),
                                    kRcDetailErrorRequestOutOfData));
    return HandleDecodeDataStatus(
        Envoy::Http::FilterDataStatus::StopIterationNoBuffer);
  }
  return {};
}

Envoy::Http::FilterHeadersStatus Filter::encodeHeaders(
    Envoy::Http::ResponseHeaderMap& headers, bool end_stream) {
  ENVOY_STREAM_LOG(debug, "Called Proto Message Logging Filter : {}",
                   *decoder_callbacks_, __func__);

  if (!extractor_ || response_logging_done_) {
    return Envoy::Http::FilterHeadersStatus::Continue;
  }

  if (end_stream) {
    const auto status = Envoy::Grpc::Common::getGrpcStatus(headers, true);
    if (status) {
      // grpc_backend_status_ = status;
    }
  } else if (Envoy::Grpc::Common::isGrpcResponseHeaders(headers, end_stream)) {
    // If it is grpc response type and need audit_extraction,
    // create response_msg_converter to convert response body.
    auto cord_message_data_factory =
        std::make_unique<CreateMessageDataFunc>([]() {
          return std::make_unique<
              google::protobuf::field_extraction::CordMessageData>();
        });

    response_msg_converter_ = std::make_unique<MessageConverter>(
        std::move(cord_message_data_factory),
        encoder_callbacks_->encoderBufferLimit());
  }

  return Envoy::Http::FilterHeadersStatus::Continue;
}

Envoy::Http::FilterDataStatus Filter::encodeData(Envoy::Buffer::Instance& data,
                                                 bool end_stream) {
  ENVOY_STREAM_LOG(debug, "encodeData: data size={} end_stream={}",
                   *decoder_callbacks_, data.length(), end_stream);

  if (!response_msg_converter_ || !extractor_) {
    return Envoy::Http::FilterDataStatus::Continue;
  }

  if (auto status = handleEncodeData(data, end_stream); !status.got_messages) {
    return status.filter_status;
  }

  return Envoy::Http::FilterDataStatus::Continue;
}

Filter::HandleDecodeDataStatus Filter::handleEncodeData(
    Envoy::Buffer::Instance& data, bool end_stream) {
  auto buffering =
      response_msg_converter_->accumulateMessages(data, end_stream);

  if (!buffering.ok()) {
    const absl::Status& status = buffering.status();
    rejectResponse(status.raw_code(), status.message(),
                   generateRcDetails(kRcDetailFilterProtoMessageLogging,
                                     absl::StatusCodeToString(status.code()),
                                     kRcDetailErrorResponseBufferConversion));
    return HandleDecodeDataStatus(
        Envoy::Http::FilterDataStatus::StopIterationNoBuffer);
  }

  if (buffering.value().empty()) {
    // Not a complete message.
    return HandleDecodeDataStatus(
        Envoy::Http::FilterDataStatus::StopIterationAndBuffer);
  }

  // Buffering returns a list of messages.
  for (size_t msg_idx = 0; msg_idx < buffering->size(); ++msg_idx) {
    std::unique_ptr<StreamMessage> stream_message =
        std::move(buffering->at(msg_idx));

    // The converter returns an empty stream_message for the last empty
    // buffer.
    if (stream_message->message() == nullptr) {
      DCHECK(end_stream);
      DCHECK(stream_message->isFinalMessage());
      // This is the last one in the vector.
      DCHECK(msg_idx == buffering->size() - 1);
      continue;
    }

    if (extractor_ && !response_logging_done_) {
      response_logging_done_ = true;
      ENVOY_STREAM_LOG(debug, "audit extract response message.",
                       *decoder_callbacks_);
      extractor_->processResponse(*stream_message->message());

      AuditResult result = extractor_->GetResult();

      if (result.response_data.empty()) {
        rejectResponse(
            Status::WellKnownGrpcStatus::InvalidArgument,
            "did not receive enough data for response logging.",
            generateRcDetails(
                kRcDetailFilterProtoMessageLogging,
                absl::StatusCodeToString(absl::StatusCode::kInvalidArgument),
                kRcDetailErrorRequestProtoMessageLoggingFailed));
        return HandleDecodeDataStatus(
            Envoy::Http::FilterDataStatus::StopIterationNoBuffer);
      }
      handleLoggingResult(result.response_data, MetadataType::RESPONSE);
    }

    auto buf_convert_status =
        response_msg_converter_->convertBackToBuffer(std::move(stream_message));

    // The stream_message is not modified, ConvertBack should NOT fail.
    RELEASE_ASSERT(buf_convert_status.ok(),
                   "response message convert back should work");

    data.move(*buf_convert_status.value());
  }

  // Reject the response if  extraction is required but could not
  // buffer up any messages.
  if (!response_logging_done_) {
    rejectResponse(Status::WellKnownGrpcStatus::InvalidArgument,
                   "did not receive enough data to form a message.",
                   generateRcDetails(kRcDetailFilterProtoMessageLogging,
                                     absl::StatusCodeToString(
                                         absl::StatusCode::kInvalidArgument),
                                     kRcDetailErrorResponseOutOfData));
    return HandleDecodeDataStatus(
        Envoy::Http::FilterDataStatus::StopIterationNoBuffer);
  }
  return HandleDecodeDataStatus(Envoy::Http::FilterDataStatus::Continue);
}

void Filter::handleLoggingResult(const std::vector<AuditMetadata>& result,
                                 MetadataType metadata_type) {
  RELEASE_ASSERT(extractor_,
                 "`extractor_ should be inited when extracting fields");

  Envoy::ProtobufWkt::Struct dest_metadata;
  for (const AuditMetadata& result_metadata : result) {
    RELEASE_ASSERT(result_metadata.target_resource.has_value(),
                   "`request_metadata.target_resource` shouldn't be empty");

    RELEASE_ASSERT(result_metadata.scrubbed_message.IsInitialized(),
                   "`request_metadata.scrubbed_message` should be initialized");

    ::google::protobuf::ListValue* list;

    if (metadata_type == MetadataType::RESPONSE) {
      list =
          (*dest_metadata.mutable_fields())["responses"].mutable_list_value();
    } else {
      list = (*dest_metadata.mutable_fields())["requests"].mutable_list_value();
    }

    auto struct_value_copy =
        new ::google::protobuf::Struct(result_metadata.scrubbed_message);
    list->add_values()->set_allocated_struct_value(struct_value_copy);
  }

  if (dest_metadata.fields_size() > 0) {
    if (metadata_type == MetadataType::RESPONSE) {
      ENVOY_STREAM_LOG(
          debug, "injected response dynamic metadata `{}` with `{}`",
          *encoder_callbacks_, kFilterName, dest_metadata.DebugString());
      encoder_callbacks_->streamInfo().setDynamicMetadata(kFilterName,
                                                          dest_metadata);
    } else {
      ENVOY_STREAM_LOG(
          debug, "injected request dynamic metadata `{}` with `{}`",
          *decoder_callbacks_, kFilterName, dest_metadata.DebugString());
      decoder_callbacks_->streamInfo().setDynamicMetadata(kFilterName,
                                                          dest_metadata);
    }
  }
}
}  // namespace ProtoMessageLogging
}  // namespace HttpFilters
}  // namespace Extensions
}  // namespace Envoy
