#include "source/extensions/filters/http/mcp/mcp_filter.h"

#include "source/common/http/headers.h"
#include "source/common/http/utility.h"
#include "source/common/protobuf/protobuf.h"
#include "source/common/protobuf/utility.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Mcp {

bool McpFilter::isValidMcpSseRequest(const Http::RequestHeaderMap& headers) const {
  // Check if this is a GET request for SSE stream
  if (headers.getMethodValue() != Http::Headers::get().MethodValues.Get) {
    return false;
  }

  // Check for Accept header containing text/event-stream
  const auto& accepts = headers.get(Http::CustomHeaders::get().Accept);
  if (accepts.empty()) {
    return false;
  }

  for (size_t i = 0; i < accepts.size(); ++i) {
    if (absl::StrContains(accepts[i]->value().getStringView(),
                          Http::Headers::get().ContentTypeValues.TextEventStream)) {
      return true;
    }
  }

  return false;
}

bool McpFilter::isValidMcpPostRequest(const Http::RequestHeaderMap& headers) const {
  // Check if this is a POST request with JSON content
  bool is_post_request =
      headers.getMethodValue() == Http::Headers::get().MethodValues.Post &&
      headers.getContentTypeValue() == Http::Headers::get().ContentTypeValues.Json;

  if (!is_post_request) {
    return false;
  }

  const auto& accepts = headers.get(Http::CustomHeaders::get().Accept);
  if (accepts.empty()) {
    return false;
  }

  // Check for Accept header containing text/event-stream and application/json
  bool has_sse = false;
  bool has_json = false;

  for (size_t i = 0; i < accepts.size(); ++i) {
    const absl::string_view value = accepts[i]->value().getStringView();
    if (!has_sse &&
        absl::StrContains(value, Http::Headers::get().ContentTypeValues.TextEventStream)) {
      has_sse = true;
    }
    if (!has_json && absl::StrContains(value, Http::Headers::get().ContentTypeValues.Json)) {
      has_json = true;
    }
    if (has_sse && has_json) {
      return true;
    }
  }

  return false;
}

bool McpFilter::shouldRejectRequest() const {
  const auto* override_config =
      Http::Utility::resolveMostSpecificPerFilterConfig<McpOverrideConfig>(decoder_callbacks_);

  if (override_config) {
    return override_config->trafficMode() ==
           envoy::extensions::filters::http::mcp::v3::Mcp::REJECT_NO_MCP;
  }

  return config_->shouldRejectNonMcp();
}

Http::FilterHeadersStatus McpFilter::decodeHeaders(Http::RequestHeaderMap& headers,
                                                   bool end_stream) {

  if (isValidMcpSseRequest(headers)) {
    is_mcp_request_ = true;
    ENVOY_LOG(debug, "valid MCP SSE request, passing through");
    return Http::FilterHeadersStatus::Continue;
  }

  if (isValidMcpPostRequest(headers)) {
    is_json_post_request_ = true;
    ENVOY_LOG(debug, "valid MCP Post request");
    if (end_stream) {
      is_mcp_request_ = false;
    } else {
      // Need to buffer the body to check for JSON-RPC 2.0
      is_mcp_request_ = true;
      return Http::FilterHeadersStatus::StopIteration;
    }
  }

  if (!is_mcp_request_ && shouldRejectRequest()) {
    ENVOY_LOG(debug, "rejecting non-MCP traffic");
    decoder_callbacks_->sendLocalReply(Http::Code::BadRequest, "Only MCP traffic is allowed",
                                       nullptr, absl::nullopt, "mcp_filter_reject_no_mcp");
    return Http::FilterHeadersStatus::StopIteration;
  }

  ENVOY_LOG(debug, "MCP filter passing through during decoding headers");
  return Http::FilterHeadersStatus::Continue;
}

Http::FilterDataStatus McpFilter::decodeData(Buffer::Instance& data, bool end_stream) {
  if (!is_json_post_request_ || !is_mcp_request_) {
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

    // Always reject for no-valid JSON requests
    if (!status.ok()) {
      is_mcp_request_ = false;
      ENVOY_LOG(debug, "failed to parse the JSON");
      decoder_callbacks_->sendLocalReply(Envoy::Http::Code::BadRequest,
                                         "Request body is not a valid JSON.", nullptr,
                                         absl::nullopt, "");
      return Http::FilterDataStatus::StopIterationNoBuffer;
    }

    // Check for JSON-RPC 2.0
    if (metadata_->fields().contains("jsonrpc") &&
        metadata_->fields().at("jsonrpc").string_value() == McpConstants::JsonRpcVersion) {
      is_mcp_request_ = true;
      // TODO(botengyao) Add more detailed MCP check.
      finalizeDynamicMetadata();
      ENVOY_LOG(debug, "valid MCP JSON-RPC 2.0 request detected");
      return Http::FilterDataStatus::Continue;
    } else {
      is_mcp_request_ = false;
      ENVOY_LOG(debug, "non-JSON-RPC 2.0 request is detected");
      if (shouldRejectRequest()) {
        decoder_callbacks_->sendLocalReply(Http::Code::BadRequest,
                                           "request must be a valid JSON-RPC 2.0 message for MCP",
                                           nullptr, absl::nullopt, "mcp_filter_not_jsonrpc");
        return Http::FilterDataStatus::StopIterationNoBuffer;
      }
      // Pass through if not rejecting
      return Http::FilterDataStatus::Continue;
    }
  }
  return Http::FilterDataStatus::StopIterationAndBuffer;
}

void McpFilter::finalizeDynamicMetadata() {
  if (metadata_ != nullptr) {
    decoder_callbacks_->streamInfo().setDynamicMetadata(std::string(MetadataKeys::FilterName),
                                                        *metadata_);
    ENVOY_LOG(debug, "MCP filter set dynamic metadata: {}", metadata_->DebugString());

    // Clear route cache to allow route re-selection based on dynamic metadata
    if (config_->clearRouteCache()) {
      if (auto cb = decoder_callbacks_->downstreamCallbacks(); cb.has_value()) {
        cb->clearRouteCache();
        ENVOY_LOG(debug, "MCP filter cleared route cache for metadata-based routing");
      }
    }
  }
}

} // namespace Mcp
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
