#include "source/extensions/filters/http/mcp/mcp_filter.h"

#include "source/common/http/headers.h"
#include "source/common/protobuf/protobuf.h"
#include "source/common/protobuf/utility.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Mcp {

Http::FilterHeadersStatus McpFilter::decodeHeaders(Http::RequestHeaderMap& headers,
                                                   bool end_stream) {
  if (headers.getMethodValue() == Http::Headers::get().MethodValues.Post &&
      headers.getContentTypeValue() == Http::Headers::get().ContentTypeValues.Json) {
    is_json_post_request_ = true;
  }

  if (!is_json_post_request_ || end_stream) {
    return Http::FilterHeadersStatus::Continue;
  }

  return Http::FilterHeadersStatus::StopIteration;
}

Http::FilterDataStatus McpFilter::decodeData(Buffer::Instance& data, bool end_stream) {
  if (!is_json_post_request_) {
    return Http::FilterDataStatus::Continue;
  }

  if (end_stream) {
    decoder_callbacks_->addDecodedData(data, true);
    std::string json = decoder_callbacks_->decodingBuffer()->toString();
    if (metadata_ == nullptr) {
      metadata_ = std::make_unique<Protobuf::Struct>();
    }
    bool has_unknown_fields = false;
    auto status = MessageUtil::loadFromJsonNoThrow(json, *metadata_, has_unknown_fields);

    if (!status.ok()) {
      decoder_callbacks_->sendLocalReply(Envoy::Http::Code::BadRequest,
                                         "Request body is not a valid JSON.", nullptr,
                                         absl::nullopt, "");
      return Http::FilterDataStatus::StopIterationNoBuffer;
    }

    if (!metadata_->fields().contains("jsonrpc") ||
        metadata_->fields().at("jsonrpc").string_value() != "2.0") {
      decoder_callbacks_->sendLocalReply(Envoy::Http::Code::BadRequest,
                                         "Request body is not a valid JSON-RPC 2.0 request.",
                                         nullptr, absl::nullopt, "");
      return Http::FilterDataStatus::StopIterationNoBuffer;
    }
    finalizeDynamicMetadata();
    return Http::FilterDataStatus::Continue;
  }

  return Http::FilterDataStatus::StopIterationAndBuffer;
}

void McpFilter::finalizeDynamicMetadata() {
  if (metadata_ != nullptr) {
    decoder_callbacks_->streamInfo().setDynamicMetadata(std::string(MetadataKeys::FilterName),
                                                        *metadata_);
    ENVOY_LOG(debug, "MCP filter set dynamic metadata: {}", metadata_->DebugString());
  }
}

} // namespace Mcp
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
