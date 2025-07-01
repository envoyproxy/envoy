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

const char kRcDetailFilterProtoApiScrubber[] = "proto_api_scrubber";
const char kRcDetailErrorRequestBufferConversion[] = "REQUEST_BUFFER_CONVERSION_FAIL";

std::string formatError(absl::string_view filter_name, absl::string_view error_type,
                        absl::string_view error_detail) {
  return absl::StrCat(filter_name, "_", error_type, "{", error_detail, "}");
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

    auto buf_convert_status =
        request_msg_converter_->convertBackToBuffer(std::move(stream_message));
    RELEASE_ASSERT(buf_convert_status.ok(), "failed to convert message back to envoy buffer");

    data.move(*buf_convert_status.value());
  }

  ENVOY_STREAM_LOG(trace, "Scrubbing completed successfully.", *decoder_callbacks_);
  return Envoy::Http::FilterDataStatus::Continue;
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
