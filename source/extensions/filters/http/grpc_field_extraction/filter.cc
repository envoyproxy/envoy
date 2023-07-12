#include "envoy/http/filter.h"

#include <memory>
#include <string>

#include "source/extensions/filters/http/grpc_field_extraction/extractor_impl.h"
#include "source/extensions/filters/http/grpc_field_extraction/extractor.h"
#include "source/extensions/filters/http/grpc_field_extraction/filter.h"
#include "absl/strings/escaping.h"
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
#include "src/message_data/cord_message_data.h"
#include "src/message_data/message_data.h"
#include "google/protobuf/io/zero_copy_stream_impl_lite.h"

namespace Envoy::Extensions::HttpFilters::GrpcFieldExtraction {
namespace {
using ::envoy::extensions::filters::http::grpc_field_extraction::v3::FieldExtractions;
using ::envoy::extensions::filters::http::grpc_field_extraction::v3::GrpcFieldExtractionConfig;
using ::Envoy::Grpc::Status;
using ::Envoy::Grpc::Utility;
using ::Envoy::ProtobufMessage::MessageConverter;

// The filter prefixes.
const char kRcDetailFilterGrpcFieldExtraction[] = "grpc_field_extraction";

const char kRcDetailErrorRequestBufferConversion[] =
    "REQUEST_BUFFER_CONVERSION_FAIL";

const char kRcDetailErrorTypeBadRequest[] = "BAD_REQUEST";

const char kRcDetailErrorRequestFieldExtractionFailed[] =
    "REQUEST_FIELD_EXTRACTION_FAILED";

const char kRcDetailErrorRequestOutOfData[] = "REQUEST_OUT_OF_DATA";

const char kGrpcFieldExtractionDynamicMetadata[] = "envoy.filters.http.grpc_field_extraction";

std::string generateRcDetails(absl::string_view filter_name,
                              absl::string_view error_type,
                              absl::string_view error_detail) {
  if (error_detail.length() > 0) {
    return absl::StrCat(filter_name, "_", error_type, "{", error_detail, "}");
  }
  return absl::StrCat(filter_name, "_", error_type);
}

// Turns a '/package.Service/method' to 'package.Service.method' which is
// the form suitable for the proto db lookup.
absl::StatusOr<std::string> GrpcPathToProtoPath(
    absl::string_view  grpc_path) {
  if (grpc_path.size() == 0 || grpc_path.at(0) != '/' ||
      std::count(grpc_path.begin(), grpc_path.end(), '/') != 2) {
    return absl::InvalidArgumentError(absl::StrFormat(
        "for grpc requests,  :path `%s` should be in form of `/package.Service/method`",
        grpc_path));
  }

  std::string clean_input = std::string(grpc_path.substr(1));
  std::replace(clean_input.begin(), clean_input.end(), '/', '.');
  return clean_input;
}

} // namespace

void Filter::rejectRequest(Status::GrpcStatus grpc_status,
                           absl::string_view error_msg,
                           absl::string_view rc_detail) {
  ENVOY_STREAM_LOG(debug,
                   "Rejecting request: grpcStatus={}, message={}",
                   *decoder_callbacks_,
                   grpc_status,
                   error_msg);
  decoder_callbacks_->sendLocalReply(
      static_cast<Envoy::Http::Code>(Utility::grpcToHttpStatus(grpc_status)),
      error_msg,
      nullptr,
      grpc_status,
      rc_detail);
}

Envoy::Http::FilterHeadersStatus Filter::decodeHeaders(Envoy::Http::RequestHeaderMap& headers,
                                                       bool) {
  if (!Grpc::Common::isGrpcRequestHeaders(headers)) {
    ENVOY_STREAM_LOG(debug,
                     "Request isn't gRPC as its headers don't have application/grpc content-type. Request is passed through "
                     "without extraction.",
                     *decoder_callbacks_);
    return Http::FilterHeadersStatus::Continue;
  }

  auto* path = headers.Path();
  if (path == nullptr) {
    ENVOY_STREAM_LOG(debug,
                     "no `:path` header in request headers. Request is passed through without extraction",
                     *decoder_callbacks_);
    return Http::FilterHeadersStatus::Continue;
  }

  auto proto_path = GrpcPathToProtoPath(path->value().getStringView());
  if (!proto_path.ok()) {
    ENVOY_STREAM_LOG(debug, "failed to convert gRPC path to protobuf path: {}", *decoder_callbacks_, proto_path.status().ToString());
                  generateRcDetails(kRcDetailFilterGrpcFieldExtraction,
                                    absl::StatusCodeToString(proto_path.status().code()),
                                    kRcDetailErrorTypeBadRequest);
    return Http::FilterHeadersStatus::StopIteration;
  }

  auto per_method_extraction = filter_config_.FindPerMethodExtraction(*proto_path);
  if (!per_method_extraction.ok()) {
    ENVOY_STREAM_LOG(debug,
                     "{}",
                     *decoder_callbacks_,
                     per_method_extraction.status().ToString());
    return Http::FilterHeadersStatus::Continue;
  }
  extractor_ = filter_config_.extractor_factory().CreateExtractor(
      filter_config_.createTypeFinder(),
      per_method_extraction->request_type,
      *per_method_extraction->field_extractions);

  request_msg_converter_ = std::make_unique<MessageConverter>(
      std::make_unique<
          std::function<std::unique_ptr<google::protobuf::field_extraction::MessageData>()>>(
          []() { return std::make_unique<google::protobuf::field_extraction::CordMessageData>(); }),
      decoder_callbacks_->decoderBufferLimit());
  headers_ = &headers;

  return Envoy::Http::FilterHeadersStatus::StopIteration;
}

Envoy::Http::FilterDataStatus Filter::decodeData(Envoy::Buffer::Instance& data,
                                                 bool end_stream) {
  if (extractor_ == nullptr) {
    return Envoy::Http::FilterDataStatus::Continue;
  }
  ENVOY_STREAM_LOG(debug,
                   "decodeData: data size={} end_stream={}",
                   *decoder_callbacks_,
                   data.length(),
                   end_stream);

  if (auto status = handleDecodeData(data, end_stream); !status.got_messages) {
    return status.filter_status;
  }

  handleExtractionResult();
  return Envoy::Http::FilterDataStatus::Continue;
}

Filter::HandleDecodeDataStatus Filter::handleDecodeData(Envoy::Buffer::Instance& data,
                                                        bool end_stream) {
  ABSL_DCHECK(extractor_);

  auto buffering = request_msg_converter_->AccumulateMessages(data, end_stream);
  if (!buffering.ok()) {
    const absl::Status& status = buffering.status();
    rejectRequest(status.raw_code(),
                  status.message(),
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
  bool got_messages = false;
  for (size_t msg_idx = 0; msg_idx < buffering->size(); ++msg_idx) {
    std::unique_ptr<ProtobufMessage::StreamMessage> message_data =
        std::move(buffering->at(msg_idx));

    if (message_data->size() == -1) {
      ABSL_DCHECK(end_stream);
      ABSL_DCHECK(message_data->is_final_message());
      // This is the last one in the vector.
      ABSL_DCHECK(msg_idx == buffering->size() - 1);
      // Skip the empty message
      continue;
    }

    got_messages = true;

    const auto status = extractor_->ProcessRequest(*message_data->message());
    if (!status.ok()) {
      rejectRequest(status.raw_code(),
                    status.message(),
                    generateRcDetails(kRcDetailFilterGrpcFieldExtraction,
                                      absl::StatusCodeToString(status.code()),
                                      kRcDetailErrorRequestFieldExtractionFailed));
      return HandleDecodeDataStatus(Envoy::Http::FilterDataStatus::StopIterationNoBuffer);
    }

    auto buf_convert_status =
        request_msg_converter_->ConvertBackToBuffer(std::move(message_data));
    // The message_data is not modified, ConvertBack should return OK.
    RELEASE_ASSERT(buf_convert_status.ok(),
                   "request message convert back should work");

    data.move(*buf_convert_status.value());
    ENVOY_STREAM_LOG(debug,
                     "decodeData: convert back data size={}",
                     *decoder_callbacks_,
                     data.length());
  }

  // Reject the request if  extraction is required but could not
  // buffer up any messages.
  if (!got_messages) {
    rejectRequest(Status::WellKnownGrpcStatus::InvalidArgument,
                  "filter did not receive enough data to form a message.",
                  generateRcDetails(kRcDetailFilterGrpcFieldExtraction,
                                    kRcDetailErrorTypeBadRequest,
                                    kRcDetailErrorRequestOutOfData));
    return HandleDecodeDataStatus(Envoy::Http::FilterDataStatus::StopIterationNoBuffer);
  }
  return HandleDecodeDataStatus();
}

void Filter::handleExtractionResult() {
  ABSL_DCHECK(extractor_);

  const auto& result = extractor_->GetResult();



 google::protobuf::Struct dest_metadata;
   for (const auto& req_field: result.req_fields) {
    auto* list =
        (*dest_metadata.mutable_fields())[req_field.field_path].mutable_list_value();
    for (const auto& value: req_field.values) {
      list->add_values()->set_string_value(value);
    }
  }
   if (dest_metadata.fields_size() > 0 ) {
   decoder_callbacks_->streamInfo().setDynamicMetadata(kGrpcFieldExtractionDynamicMetadata,  dest_metadata);
   }
}



} // namespace Envoy::Extensions::HttpFilters::GrpcFieldExtraction