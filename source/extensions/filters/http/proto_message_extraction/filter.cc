#include "source/extensions/filters/http/proto_message_extraction/filter.h"

#include <algorithm>
#include <cstddef>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "envoy/http/filter.h"
#include "envoy/http/header_map.h"
#include "envoy/registry/registry.h"
#include "envoy/stream_info/filter_state.h"

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
#include "source/extensions/filters/http/proto_message_extraction/extraction_util/proto_extractor_interface.h"
#include "source/extensions/filters/http/proto_message_extraction/extractor.h"

#include "absl/log/check.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "proto_field_extraction/message_data/cord_message_data.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ProtoMessageExtraction {
namespace {

using ::Envoy::Extensions::HttpFilters::GrpcFieldExtraction::CreateMessageDataFunc;
using ::Envoy::Extensions::HttpFilters::GrpcFieldExtraction::MessageConverter;
using ::Envoy::Extensions::HttpFilters::GrpcFieldExtraction::StreamMessage;
using ::Envoy::Grpc::Status;
using ::Envoy::Grpc::Utility;

// The filter prefixes.
const char kRcDetailFilterProtoMessageExtraction[] = "proto_message_extraction";

const char kRcDetailErrorTypeBadRequest[] = "BAD_REQUEST";

const char kRcDetailErrorRequestOutOfData[] = "REQUEST_OUT_OF_DATA";

const char kRcDetailErrorResponseOutOfData[] = "RESPONSE_OUT_OF_DATA";

const char kRcDetailErrorRequestBufferConversion[] = "REQUEST_BUFFER_CONVERSION_FAIL";

const char kRcDetailErrorResponseBufferConversion[] = "RESPONSE_BUFFER_CONVERSION_FAIL";

// Turns a '/package.service/method' to 'package.service.method' which is
// the form suitable for the proto db lookup.
absl::StatusOr<std::string> grpcPathToProtoPath(absl::string_view grpc_path) {
  if (grpc_path.empty() || grpc_path.at(0) != '/' ||
      std::count(grpc_path.begin(), grpc_path.end(), '/') != 2) {
    return absl::InvalidArgumentError(
        absl::StrFormat(":path `%s` should be in form of `/package.service/method`", grpc_path));
  }

  std::string clean_input = std::string(grpc_path.substr(1));
  std::replace(clean_input.begin(), clean_input.end(), '/', '.');
  return clean_input;
}

std::string generateRcDetails(absl::string_view filter_name, absl::string_view error_type,
                              absl::string_view error_detail) {
  return absl::StrCat(filter_name, "_", error_type, "{", error_detail, "}");
}
} // namespace

void Filter::rejectRequest(Envoy::Grpc::Status::GrpcStatus grpc_status, absl::string_view error_msg,
                           absl::string_view rc_detail) {
  ENVOY_STREAM_LOG(debug, "Rejecting request: grpcStatus={}, message={}", *decoder_callbacks_,
                   grpc_status, error_msg);
  decoder_callbacks_->sendLocalReply(
      static_cast<Envoy::Http::Code>(Utility::grpcToHttpStatus(grpc_status)), error_msg, nullptr,
      grpc_status, rc_detail);
}

void Filter::rejectResponse(Status::GrpcStatus grpc_status, absl::string_view error_msg,
                            absl::string_view rc_detail) {
  ENVOY_STREAM_LOG(debug, "Rejecting response grpcStatus={}, message={}", *encoder_callbacks_,
                   grpc_status, error_msg);

  encoder_callbacks_->sendLocalReply(
      static_cast<Envoy::Http::Code>(Utility::grpcToHttpStatus(grpc_status)), error_msg, nullptr,
      grpc_status, rc_detail);

  encoder_callbacks_->streamInfo().setResponseFlag(
      Envoy::StreamInfo::CoreResponseFlag::UnauthorizedExternalService);
}

Envoy::Http::FilterHeadersStatus Filter::decodeHeaders(Envoy::Http::RequestHeaderMap& headers,
                                                       bool) {
  ENVOY_STREAM_LOG(debug, "Called Proto Message Extraction Filter : {}", *decoder_callbacks_,
                   __func__);

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
  auto proto_path = grpcPathToProtoPath(headers.Path()->value().getStringView());
  if (!proto_path.ok()) {
    ENVOY_STREAM_LOG(info, "failed to convert gRPC path to protobuf path: {}", *decoder_callbacks_,
                     proto_path.status().ToString());

    auto& status = proto_path.status();
    rejectRequest(status.raw_code(), status.message(),
                  generateRcDetails(kRcDetailFilterProtoMessageExtraction,
                                    absl::StatusCodeToString(status.code()),
                                    kRcDetailErrorTypeBadRequest));
    return Envoy::Http::FilterHeadersStatus::StopIteration;
  }

  const auto* extractor = filter_config_.findExtractor(*proto_path);
  if (!extractor) {
    ENVOY_STREAM_LOG(debug,
                     "gRPC method `{}` isn't configured for Proto Message Extraction filter ",
                     *decoder_callbacks_, *proto_path);
    return Envoy::Http::FilterHeadersStatus::Continue;
  }

  // Cast away const for necessary modifications in a controlled context.
  extractor_ = const_cast<Extractor*>(extractor);
  auto cord_message_data_factory = std::make_unique<CreateMessageDataFunc>(
      []() { return std::make_unique<Protobuf::field_extraction::CordMessageData>(); });

  request_msg_converter_ = std::make_unique<MessageConverter>(
      std::move(cord_message_data_factory), decoder_callbacks_->decoderBufferLimit());

  return Envoy::Http::FilterHeadersStatus::StopIteration;
}

Envoy::Http::FilterDataStatus Filter::decodeData(Envoy::Buffer::Instance& data, bool end_stream) {
  ENVOY_STREAM_LOG(debug, "decodeData: data size={} end_stream={}", *decoder_callbacks_,
                   data.length(), end_stream);

  if (!extractor_) {
    return Envoy::Http::FilterDataStatus::Continue;
  }

  if (auto status = handleDecodeData(data, end_stream); !status.got_messages) {
    return status.filter_status;
  }

  return Envoy::Http::FilterDataStatus::Continue;
}

Filter::HandleDataStatus Filter::handleDecodeData(Envoy::Buffer::Instance& data, bool end_stream) {
  RELEASE_ASSERT(extractor_ && request_msg_converter_,
                 "`extractor_` and request_msg_converter_ both should be "
                 "initiated when extracting proto messages");

  auto buffering = request_msg_converter_->accumulateMessages(data, end_stream);
  if (!buffering.ok()) {
    const absl::Status& status = buffering.status();
    rejectRequest(status.raw_code(), status.message(),
                  generateRcDetails(kRcDetailFilterProtoMessageExtraction,
                                    absl::StatusCodeToString(status.code()),
                                    kRcDetailErrorRequestBufferConversion));
    return HandleDataStatus(Envoy::Http::FilterDataStatus::StopIterationNoBuffer);
  }

  if (buffering->empty()) {
    ENVOY_STREAM_LOG(debug, "not a complete msg", *decoder_callbacks_);
    // Not a complete message.
    return HandleDataStatus(Envoy::Http::FilterDataStatus::StopIterationNoBuffer);
  }

  // Buffering returns a list of messages.
  for (size_t msg_idx = 0; msg_idx < buffering->size(); ++msg_idx) {
    std::unique_ptr<StreamMessage> message_data = std::move(buffering->at(msg_idx));

    // MessageConverter uses an empty StreamMessage to denote the end.
    if (message_data->message() == nullptr) {
      RELEASE_ASSERT(end_stream, "expect end_stream=true as when the MessageConverter "
                                 "signals an stream end");
      RELEASE_ASSERT(message_data->isFinalMessage(),
                     "expect message_data->isFinalMessage()=true when the "
                     "MessageConverter signals "
                     "an stream end");
      // This is the last one in the vector.
      RELEASE_ASSERT(msg_idx == buffering->size() - 1,
                     "expect message_data is the last element in the vector when the "
                     "MessageConverter signals an stream end");
      // Skip the empty message
      continue;
    }
    // Set the request_extraction_done_ to true since we have received a message for extraction.
    request_extraction_done_ = true;

    extractor_->processRequest(*message_data->message());

    ExtractedMessageResult result = extractor_->GetResult();

    if (result.request_data.empty()) {
      return HandleDataStatus(Envoy::Http::FilterDataStatus::StopIterationNoBuffer);
    }
    handleRequestExtractionResult(result.request_data);

    auto buf_convert_status = request_msg_converter_->convertBackToBuffer(std::move(message_data));
    // The message_data is not modified, ConvertBack should return OK.
    RELEASE_ASSERT(buf_convert_status.ok(), "request message convert back should work");

    data.move(*buf_convert_status.value());
    ENVOY_STREAM_LOG(debug, "decodeData: convert back data size={}", *decoder_callbacks_,
                     data.length());
  }

  // Reject the request if  extraction is required but could not
  // buffer up any messages.
  if (!request_extraction_done_) {
    rejectRequest(Status::WellKnownGrpcStatus::InvalidArgument,
                  "did not receive enough data to form a message.",
                  generateRcDetails(kRcDetailFilterProtoMessageExtraction,
                                    absl::StatusCodeToString(absl::StatusCode::kInvalidArgument),
                                    kRcDetailErrorRequestOutOfData));
    return HandleDataStatus(Envoy::Http::FilterDataStatus::StopIterationNoBuffer);
  }
  return HandleDataStatus(Envoy::Http::FilterDataStatus::Continue);
}

Envoy::Http::FilterHeadersStatus Filter::encodeHeaders(Envoy::Http::ResponseHeaderMap& headers,
                                                       bool end_stream) {
  ENVOY_STREAM_LOG(debug, "Called Proto Message Extraction Filter : {}", *encoder_callbacks_,
                   __func__);

  if (!Envoy::Grpc::Common::isGrpcResponseHeaders(headers, end_stream)) {
    ENVOY_STREAM_LOG(
        debug,
        "Response headers is NOT application/grpc content-type. Response is passed through "
        "without message extraction.",
        *encoder_callbacks_);
    return Envoy::Http::FilterHeadersStatus::Continue;
  }

  if (!extractor_) {
    return Envoy::Http::FilterHeadersStatus::Continue;
  }

  // Create response_msg_converter to convert response body.
  auto cord_message_data_factory = std::make_unique<CreateMessageDataFunc>(
      []() { return std::make_unique<Protobuf::field_extraction::CordMessageData>(); });

  response_msg_converter_ = std::make_unique<MessageConverter>(
      std::move(cord_message_data_factory), encoder_callbacks_->encoderBufferLimit());

  return Http::FilterHeadersStatus::StopIteration;
}

Envoy::Http::FilterDataStatus Filter::encodeData(Envoy::Buffer::Instance& data, bool end_stream) {
  ENVOY_STREAM_LOG(debug, "encodeData: data size={} end_stream={}", *decoder_callbacks_,
                   data.length(), end_stream);

  if (!response_msg_converter_ || !extractor_) {
    return Envoy::Http::FilterDataStatus::Continue;
  }

  if (auto status = handleEncodeData(data, end_stream); !status.got_messages) {
    return status.filter_status;
  }

  return Envoy::Http::FilterDataStatus::Continue;
}

Filter::HandleDataStatus Filter::handleEncodeData(Envoy::Buffer::Instance& data, bool end_stream) {
  RELEASE_ASSERT(extractor_ && response_msg_converter_,
                 "`extractor_` and response_msg_converter_ both should be "
                 "initiated when extracting proto messages");

  auto buffering = response_msg_converter_->accumulateMessages(data, end_stream);

  if (!buffering.ok()) {
    const absl::Status& status = buffering.status();
    rejectResponse(status.raw_code(), status.message(),
                   generateRcDetails(kRcDetailFilterProtoMessageExtraction,
                                     absl::StatusCodeToString(status.code()),
                                     kRcDetailErrorResponseBufferConversion));
    return HandleDataStatus(Envoy::Http::FilterDataStatus::StopIterationNoBuffer);
  }

  if (buffering->empty()) {
    ENVOY_STREAM_LOG(debug, "not a complete msg", *encoder_callbacks_);
    // Not a complete message.
    return HandleDataStatus(Envoy::Http::FilterDataStatus::StopIterationNoBuffer);
  }

  // Buffering returns a list of messages.
  for (size_t msg_idx = 0; msg_idx < buffering->size(); ++msg_idx) {
    std::unique_ptr<StreamMessage> stream_message = std::move(buffering->at(msg_idx));

    // The converter returns an empty stream_message for the last empty
    // buffer.
    if (stream_message->message() == nullptr) {
      DCHECK(end_stream);
      DCHECK(stream_message->isFinalMessage());
      // This is the last one in the vector.
      DCHECK(msg_idx == buffering->size() - 1);
      continue;
    }

    // Set the response_extraction_done_ to true since we have received a message for extraction.
    response_extraction_done_ = true;

    extractor_->processResponse(*stream_message->message());

    ExtractedMessageResult result = extractor_->GetResult();

    if (result.response_data.empty()) {
      return HandleDataStatus(Envoy::Http::FilterDataStatus::StopIterationNoBuffer);
    }
    handleResponseExtractionResult(result.response_data);

    auto buf_convert_status =
        response_msg_converter_->convertBackToBuffer(std::move(stream_message));

    // The stream_message is not modified, ConvertBack should NOT fail.
    RELEASE_ASSERT(buf_convert_status.ok(), "response message convert back should work");

    data.move(*buf_convert_status.value());
  }

  // Reject the response if  extraction is required but could not
  // buffer up any messages.
  if (!response_extraction_done_) {
    rejectResponse(Status::WellKnownGrpcStatus::InvalidArgument,
                   "did not receive enough data to form a message.",
                   generateRcDetails(kRcDetailFilterProtoMessageExtraction,
                                     absl::StatusCodeToString(absl::StatusCode::kInvalidArgument),
                                     kRcDetailErrorResponseOutOfData));
    return HandleDataStatus(Envoy::Http::FilterDataStatus::StopIterationNoBuffer);
  }
  return HandleDataStatus(Envoy::Http::FilterDataStatus::Continue);
}

void Filter::handleRequestExtractionResult(const std::vector<ExtractedMessageMetadata>& result) {
  RELEASE_ASSERT(extractor_, "`extractor_` should be initialized when extracting fields");

  Envoy::ProtobufWkt::Struct dest_metadata;

  auto addResultToMetadata = [&](const std::string& category, const std::string& key,
                                 const ExtractedMessageMetadata& metadata) {
    RELEASE_ASSERT(metadata.extracted_message.IsInitialized(),
                   "`extracted_message` should be initialized");

    auto* category_field = (*dest_metadata.mutable_fields())[category].mutable_struct_value();

    auto* key_field = (*category_field->mutable_fields())[key].mutable_struct_value();

    for (const auto& field : metadata.extracted_message.fields()) {
      (*key_field->mutable_fields())[field.first] = field.second;
    }
  };

  const auto& first_metadata = result[0];
  addResultToMetadata("requests", "first", first_metadata);

  if (result.size() == 2) {
    const auto& last_metadata = result[1];
    addResultToMetadata("requests", "last", last_metadata);
  }

  if (dest_metadata.fields_size() > 0) {
    ENVOY_STREAM_LOG(debug, "Injected request dynamic metadata `{}` with `{}`", *decoder_callbacks_,
                     kFilterName, dest_metadata.DebugString());
    decoder_callbacks_->streamInfo().setDynamicMetadata(kFilterName, dest_metadata);
  }
}

void Filter::handleResponseExtractionResult(const std::vector<ExtractedMessageMetadata>& result) {
  RELEASE_ASSERT(extractor_, "`extractor_` should be initialized when extracting fields");

  Envoy::ProtobufWkt::Struct dest_metadata;

  auto addResultToMetadata = [&](const std::string& category, const std::string& key,
                                 const ExtractedMessageMetadata& metadata) {
    RELEASE_ASSERT(metadata.extracted_message.IsInitialized(),
                   "`extracted_message` should be initialized");

    auto* category_field = (*dest_metadata.mutable_fields())[category].mutable_struct_value();

    auto* key_field = (*category_field->mutable_fields())[key].mutable_struct_value();

    for (const auto& field : metadata.extracted_message.fields()) {
      (*key_field->mutable_fields())[field.first] = field.second;
    }
  };

  const auto& first_metadata = result[0];
  addResultToMetadata("responses", "first", first_metadata);

  if (result.size() == 2) {
    const auto& last_metadata = result[1];
    addResultToMetadata("responses", "last", last_metadata);
  }

  if (dest_metadata.fields_size() > 0) {
    ENVOY_STREAM_LOG(debug, "Injected response dynamic metadata `{}` with `{}`",
                     *decoder_callbacks_, kFilterName, dest_metadata.DebugString());
    encoder_callbacks_->streamInfo().setDynamicMetadata(kFilterName, dest_metadata);
  }
}

} // namespace ProtoMessageExtraction
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
