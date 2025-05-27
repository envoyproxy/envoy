#include "source/extensions/filters/http/proto_api_scrubber/filter.h"

#include "envoy/http/filter.h"
#include "envoy/http/header_map.h"

#include "source/common/grpc/common.h"
#include "source/common/grpc/status.h"
#include "source/extensions/filters/http/grpc_field_extraction/message_converter/message_converter.h"
#include "source/extensions/filters/http/grpc_field_extraction/message_converter/message_converter_utility.h"
#include "source/extensions/filters/http/grpc_field_extraction/message_converter/stream_message.h"

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

const char kRcDetailErrorRequestOutOfData[] = "REQUEST_OUT_OF_DATA";

const char kRcDetailErrorRequestBufferConversion[] = "REQUEST_BUFFER_CONVERSION_FAIL";

std::string generateRcDetails(absl::string_view filter_name, absl::string_view error_type,
                              absl::string_view error_detail) {
  return absl::StrCat(filter_name, "_", error_type, "{", error_detail, "}");
}
} // namespace

ProtoApiScrubberFilter::ProtoApiScrubberFilter(ProtoApiScrubberFilterConfig&) {}

Http::FilterHeadersStatus
ProtoApiScrubberFilter::decodeHeaders(Envoy::Http::RequestHeaderMap& headers, bool) {
  ENVOY_STREAM_LOG(debug, "Called Proto Message Extraction Filter : {}", *decoder_callbacks_,
                   __func__);

  if (!Envoy::Grpc::Common::isGrpcRequestHeaders(headers)) {
    ENVOY_STREAM_LOG(debug,
                     "Request isn't gRPC as its headers don't have application/grpc content-type. "
                     "Passed through the request without scrubbing.",
                     *decoder_callbacks_);
    return Envoy::Http::FilterHeadersStatus::Continue;
  }

  auto cord_message_data_factory = std::make_unique<CreateMessageDataFunc>(
      []() { return std::make_unique<Protobuf::field_extraction::CordMessageData>(); });

  request_msg_converter_ = std::make_unique<MessageConverter>(
      std::move(cord_message_data_factory), decoder_callbacks_->decoderBufferLimit());

  return Envoy::Http::FilterHeadersStatus::Continue;
}

Http::FilterDataStatus ProtoApiScrubberFilter::decodeData(Buffer::Instance& data, bool end_stream) {
  ENVOY_STREAM_LOG(debug, "Called ProtoApiScrubber::decodeData: data size={} end_stream={}",
                   *decoder_callbacks_, data.length(), end_stream);

  // Buffer the data to complete the request message.
  auto buffering = request_msg_converter_->accumulateMessages(data, end_stream);
  if (!buffering.ok()) {
    const absl::Status& status = buffering.status();
    rejectRequest(status.raw_code(), status.message(),
                  generateRcDetails(kRcDetailFilterProtoApiScrubber,
                                    absl::StatusCodeToString(status.code()),
                                    kRcDetailErrorRequestBufferConversion));
    return Envoy::Http::FilterDataStatus::StopIterationNoBuffer;
  }

  if (buffering->empty()) {
    ENVOY_STREAM_LOG(debug, "not a complete msg", *decoder_callbacks_);
    // Not a complete message.
    return Http::FilterDataStatus::StopIterationNoBuffer;
  }

  // Buffering returns a list of messages.
  // Process them individually, one by one.
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

    // Set the request_scrubbing_done_ to true since we have received a message for extraction.
    request_scrubbing_done_ = true;

    // Call the ProtoScrubber.Scrub(message_data->message()) with reference.
    // extractor_->processRequest(*message_data->message());
    // ExtractedMessageResult result = extractor_->GetResult();
    /*
    if (result.request_data.empty()) {
      return HandleDataStatus(Envoy::Http::FilterDataStatus::StopIterationNoBuffer);
    }
    handleRequestExtractionResult(result.request_data);
    */

    auto buf_convert_status = request_msg_converter_->convertBackToBuffer(std::move(message_data));

    RELEASE_ASSERT(buf_convert_status.ok(), "request message convert back should work");

    data.move(*buf_convert_status.value());
    ENVOY_STREAM_LOG(debug, "decodeData: convert back data size={}", *decoder_callbacks_,
                     data.length());
  }

  // Reject the request if  extraction is required but could not
  // buffer up any messages.
  if (!request_scrubbing_done_) {
    rejectRequest(Status::WellKnownGrpcStatus::InvalidArgument,
                  "did not receive enough data to form a message.",
                  generateRcDetails(kRcDetailFilterProtoApiScrubber,
                                    absl::StatusCodeToString(absl::StatusCode::kInvalidArgument),
                                    kRcDetailErrorRequestOutOfData));
    return Envoy::Http::FilterDataStatus::StopIterationNoBuffer;
  }
  return Envoy::Http::FilterDataStatus::Continue;

  // Instantiate and create FieldCheckerInterface

  return Http::FilterDataStatus::Continue;
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
