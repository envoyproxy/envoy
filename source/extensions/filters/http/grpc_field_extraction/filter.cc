#include "source/extensions/filters/http/grpc_field_extraction/filter.h"

#include <memory>
#include <string>

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
#include "source/extensions/filters/http/grpc_field_extraction/extractor.h"

#include "absl/strings/escaping.h"
#include "absl/strings/str_format.h"
#include "proto_field_extraction/message_data/cord_message_data.h"
#include "proto_field_extraction/message_data/message_data.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace GrpcFieldExtraction {
namespace {
using ::Envoy::Grpc::Status;
using ::Envoy::Grpc::Utility;

// The filter prefixes.
const char kRcDetailFilterGrpcFieldExtraction[] = "grpc_field_extraction";

const char kRcDetailErrorRequestBufferConversion[] = "REQUEST_BUFFER_CONVERSION_FAIL";

const char kRcDetailErrorTypeBadRequest[] = "BAD_REQUEST";

const char kRcDetailErrorRequestFieldExtractionFailed[] = "REQUEST_FIELD_EXTRACTION_FAILED";

const char kRcDetailErrorRequestOutOfData[] = "REQUEST_OUT_OF_DATA";

std::string generateRcDetails(absl::string_view filter_name, absl::string_view error_type,
                              absl::string_view error_detail) {
  return absl::StrCat(filter_name, "_", error_type, "{", error_detail, "}");
}

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

} // namespace

void Filter::rejectRequest(Status::GrpcStatus grpc_status, absl::string_view error_msg,
                           absl::string_view rc_detail) {
  ENVOY_STREAM_LOG(debug, "Rejecting request: grpcStatus={}, message={}", *decoder_callbacks_,
                   grpc_status, error_msg);
  decoder_callbacks_->sendLocalReply(
      static_cast<Envoy::Http::Code>(Utility::grpcToHttpStatus(grpc_status)), error_msg, nullptr,
      grpc_status, rc_detail);
}

Envoy::Http::FilterHeadersStatus Filter::decodeHeaders(Envoy::Http::RequestHeaderMap& headers,
                                                       bool) {
  if (!Grpc::Common::isGrpcRequestHeaders(headers)) {
    ENVOY_STREAM_LOG(debug,
                     "Request isn't gRPC as its headers don't have application/grpc content-type. "
                     "Passed through the request "
                     "without extraction.",
                     *decoder_callbacks_);
    return Http::FilterHeadersStatus::Continue;
  }

  // Grpc::Common::isGrpcRequestHeaders above already ensures the existence of ":path" header.
  auto proto_path = grpcPathToProtoPath(headers.Path()->value().getStringView());
  if (!proto_path.ok()) {
    ENVOY_STREAM_LOG(info, "failed to convert gRPC path to protobuf path: {}", *decoder_callbacks_,
                     proto_path.status().ToString());
    auto& status = proto_path.status();
    rejectRequest(status.raw_code(), status.message(),
                  generateRcDetails(kRcDetailFilterGrpcFieldExtraction,
                                    absl::StatusCodeToString(status.code()),
                                    kRcDetailErrorTypeBadRequest));
    return Http::FilterHeadersStatus::StopIteration;
  }

  auto* extractor = filter_config_->findExtractor(*proto_path);
  if (!extractor) {
    ENVOY_STREAM_LOG(debug, "gRPC method `{}` isn't configured for field extraction",
                     *decoder_callbacks_, *proto_path);
    return Http::FilterHeadersStatus::Continue;
  }

  extractor_ = extractor;
  auto cord_message_data_factory = std::make_unique<CreateMessageDataFunc>(
      []() { return std::make_unique<Protobuf::field_extraction::CordMessageData>(); });
  request_msg_converter_ = std::make_unique<MessageConverter>(
      std::move(cord_message_data_factory), decoder_callbacks_->decoderBufferLimit());

  return Envoy::Http::FilterHeadersStatus::StopIteration;
}

Envoy::Http::FilterDataStatus Filter::decodeData(Envoy::Buffer::Instance& data, bool end_stream) {
  ENVOY_STREAM_LOG(debug, "decodeData: data size={} end_stream={}", *decoder_callbacks_,
                   data.length(), end_stream);
  if (!extractor_ || extraction_done_) {
    return Envoy::Http::FilterDataStatus::Continue;
  }

  if (auto status = handleDecodeData(data, end_stream); !status.got_messages) {
    return status.filter_status;
  }

  return Envoy::Http::FilterDataStatus::Continue;
}

Filter::HandleDecodeDataStatus Filter::handleDecodeData(Envoy::Buffer::Instance& data,
                                                        bool end_stream) {
  RELEASE_ASSERT(
      extractor_ && request_msg_converter_,
      "both `extractor_` abd `request_msg_converter_` should be inited when extracting fields");

  auto buffering = request_msg_converter_->accumulateMessages(data, end_stream);
  if (!buffering.ok()) {
    const absl::Status& status = buffering.status();
    rejectRequest(status.raw_code(), status.message(),
                  generateRcDetails(kRcDetailFilterGrpcFieldExtraction,
                                    absl::StatusCodeToString(status.code()),
                                    kRcDetailErrorRequestBufferConversion));
    return HandleDecodeDataStatus(Envoy::Http::FilterDataStatus::StopIterationNoBuffer);
  }

  if (buffering->empty()) {
    ENVOY_STREAM_LOG(debug, "not a complete msg", *decoder_callbacks_);
    // Not a complete message.
    return HandleDecodeDataStatus(Envoy::Http::FilterDataStatus::StopIterationNoBuffer);
  }

  // Buffering returns a list of messages.
  for (size_t msg_idx = 0; msg_idx < buffering->size(); ++msg_idx) {
    std::unique_ptr<StreamMessage> message_data = std::move(buffering->at(msg_idx));

    // MessageConverter uses a empty StreamMessage to denote the end.
    if (message_data->message() == nullptr) {
      RELEASE_ASSERT(end_stream,
                     "expect end_stream=true as when the MessageConverter signals an stream end");
      RELEASE_ASSERT(message_data->isFinalMessage(),
                     "expect message_data->isFinalMessage()=true when the MessageConverter signals "
                     "an stream end");
      // This is the last one in the vector.
      RELEASE_ASSERT(msg_idx == buffering->size() - 1,
                     "expect message_data is the last element in the vector when the "
                     "MessageConverter signals an stream end");
      // Skip the empty message
      continue;
    }

    if (!extraction_done_) {
      extraction_done_ = true;

      auto result = extractor_->processRequest(*message_data->message());
      if (!result.ok()) {
        rejectRequest(result.status().raw_code(), result.status().message(),
                      generateRcDetails(kRcDetailFilterGrpcFieldExtraction,
                                        absl::StatusCodeToString(result.status().code()),
                                        kRcDetailErrorRequestFieldExtractionFailed));
        return HandleDecodeDataStatus(Envoy::Http::FilterDataStatus::StopIterationNoBuffer);
      }
      handleExtractionResult(*result);
    }

    auto buf_convert_status = request_msg_converter_->convertBackToBuffer(std::move(message_data));
    // The message_data is not modified, ConvertBack should return OK.
    RELEASE_ASSERT(buf_convert_status.ok(), "request message convert back should work");

    data.move(*buf_convert_status.value());
    ENVOY_STREAM_LOG(debug, "decodeData: convert back data size={}", *decoder_callbacks_,
                     data.length());
  }

  // Reject the request if  extraction is required but could not
  // buffer up any messages.
  if (!extraction_done_) {
    rejectRequest(Status::WellKnownGrpcStatus::InvalidArgument,
                  "did not receive enough data to form a message.",
                  generateRcDetails(kRcDetailFilterGrpcFieldExtraction,
                                    absl::StatusCodeToString(absl::StatusCode::kInvalidArgument),
                                    kRcDetailErrorRequestOutOfData));
    return HandleDecodeDataStatus(Envoy::Http::FilterDataStatus::StopIterationNoBuffer);
  }
  return {};
}

void Filter::handleExtractionResult(const ExtractionResult& result) {
  RELEASE_ASSERT(extractor_, "`extractor_ should be inited when extracting fields");

  ProtobufWkt::Struct dest_metadata;
  for (const auto& req_field : result) {
    RELEASE_ASSERT(!req_field.path.empty(), "`req_field.path` shouldn't be empty");
    (*dest_metadata.mutable_fields())[req_field.path] = req_field.value;
  }
  if (dest_metadata.fields_size() > 0) {
    ENVOY_STREAM_LOG(debug, "injected dynamic metadata `{}` with `{}`", *decoder_callbacks_,
                     kFilterName, dest_metadata.DebugString());
    decoder_callbacks_->streamInfo().setDynamicMetadata(kFilterName, dest_metadata);
  }
}

} // namespace GrpcFieldExtraction
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
