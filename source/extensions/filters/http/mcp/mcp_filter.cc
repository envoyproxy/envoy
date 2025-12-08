#include "source/extensions/filters/http/mcp/mcp_filter.h"

#include "source/common/http/headers.h"
#include "source/common/http/utility.h"
#include "source/common/protobuf/protobuf.h"
#include "source/common/protobuf/utility.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Mcp {

namespace {
McpFilterStats generateStats(const std::string& prefix, Stats::Scope& scope) {
  const std::string final_prefix = absl::StrCat(prefix, "mcp.");
  return McpFilterStats{MCP_FILTER_STATS(POOL_COUNTER_PREFIX(scope, final_prefix))};
}
} // namespace

McpFilterConfig::McpFilterConfig(const envoy::extensions::filters::http::mcp::v3::Mcp& proto_config,
                                 const std::string& stats_prefix, Stats::Scope& scope)
    : traffic_mode_(proto_config.traffic_mode()),
      clear_route_cache_(proto_config.clear_route_cache()),
      max_request_body_size_(proto_config.has_max_request_body_size()
                                 ? proto_config.max_request_body_size().value()
                                 : 8192), // Default: 8KB
      parser_config_(proto_config.has_parser_config()
                         ? McpParserConfig::fromProto(proto_config.parser_config())
                         : McpParserConfig::createDefault()),
      stats_(generateStats(stats_prefix, scope)) {}

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

uint32_t McpFilter::getMaxRequestBodySize() const {
  const auto* override_config =
      Http::Utility::resolveMostSpecificPerFilterConfig<McpOverrideConfig>(decoder_callbacks_);

  if (override_config && override_config->maxRequestBodySize().has_value()) {
    return override_config->maxRequestBodySize().value();
  }

  return config_->maxRequestBodySize();
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

      // Set the buffer limit - Envoy will automatically send 413 if exceeded
      const uint32_t max_size = getMaxRequestBodySize();
      if (max_size > 0) {
        decoder_callbacks_->setDecoderBufferLimit(max_size);
        ENVOY_LOG(debug, "set decoder buffer limit to {} bytes", max_size);
      }

      return Http::FilterHeadersStatus::StopIteration;
    }
  }

  ENVOY_LOG(debug, "after the post check");
  if (!is_mcp_request_ && shouldRejectRequest()) {
    ENVOY_LOG(debug, "rejecting non-MCP traffic");
    config_->stats().requests_rejected_.inc();
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

  if (!parser_) {
    parser_ = std::make_unique<JsonPathParser>(config_->parserConfig());
  }

  if (parsing_complete_) {
    return Http::FilterDataStatus::Continue;
  }

  size_t buffer_size = data.length();

  ENVOY_LOG(trace, "decodeData: buffer_size={}, already_parsed={}", buffer_size, bytes_parsed_);

  const uint32_t max_size = getMaxRequestBodySize();
  size_t to_parse = buffer_size - bytes_parsed_;
  if (max_size > 0) {
    if (bytes_parsed_ >= max_size) {
      config_->stats().body_too_large_.inc();
      handleParseError("request body is too large.");
      return Http::FilterDataStatus::StopIterationNoBuffer;
    }
    size_t remaining_limit = max_size - bytes_parsed_;
    to_parse = std::min(to_parse, remaining_limit);
  }

  std::string parse_buffer;
  parse_buffer.resize(to_parse);
  data.copyOut(bytes_parsed_, to_parse, parse_buffer.data());

  // The partial parser will return an OK status if the requirements are not satisfied.
  // It will potentially be a bad status due to the partial parse if all the requirements
  // are extracted.
  auto status = parser_->parse(parse_buffer);
  bytes_parsed_ += to_parse;

  if (parser_->isAllFieldsCollected()) {
    ENVOY_LOG(debug, "mcp early parse termination: found all fields");
    return completeParsing();
  }

  // A non-JSON data is received
  if (!status.ok()) {
    decoder_callbacks_->sendLocalReply(Http::Code::BadRequest, "not a valid JSON", nullptr,
                                       absl::nullopt, "mcp_filter_not_jsonrpc");
    return Http::FilterDataStatus::StopIterationNoBuffer;
  }

  // If we are here, we haven't collected all fields yet.
  bool size_limit_hit = (max_size > 0 && bytes_parsed_ >= max_size);
  if (end_stream || size_limit_hit) {
    auto final_status = parser_->finishParse();
    if (!final_status.ok()) {
      config_->stats().body_too_large_.inc();
      handleParseError("reached end_stream or configured body size, don't get enough data.");
      return Http::FilterDataStatus::StopIterationNoBuffer;
    }
    return completeParsing();
  }

  return Http::FilterDataStatus::StopIterationAndBuffer;
}

void McpFilter::handleParseError(absl::string_view error_msg) {
  ENVOY_LOG(debug, "parse error: {}", error_msg);

  is_mcp_request_ = false;

  decoder_callbacks_->sendLocalReply(Http::Code::BadRequest, error_msg, nullptr, absl::nullopt,
                                     "mcp_filter_parse_error");
}

Http::FilterDataStatus McpFilter::completeParsing() {
  parsing_complete_ = true;
  is_mcp_request_ = parser_->isValidMcpRequest();

  ENVOY_LOG(debug, "parsing complete: is_mcp={}, bytes_parsed={}", is_mcp_request_, bytes_parsed_);

  if (!is_mcp_request_ && shouldRejectRequest()) {
    decoder_callbacks_->sendLocalReply(Http::Code::BadRequest,
                                       "request must be a valid JSON-RPC 2.0 message for MCP",
                                       nullptr, absl::nullopt, "mcp_filter_not_jsonrpc");
    return Http::FilterDataStatus::StopIterationNoBuffer;
  }

  // Set dynamic metadata
  const auto& metadata = parser_->metadata();
  if (!metadata.fields().empty()) {
    decoder_callbacks_->streamInfo().setDynamicMetadata(std::string(MetadataKeys::FilterName),
                                                        metadata);
    ENVOY_LOG(debug, "MCP filter set dynamic metadata: {}", metadata.DebugString());

    if (config_->clearRouteCache()) {
      if (auto cb = decoder_callbacks_->downstreamCallbacks(); cb.has_value()) {
        cb->clearRouteCache();
        ENVOY_LOG(debug, "MCP filter cleared route cache for metadata-based routing");
      }
    }
  }
  return Http::FilterDataStatus::Continue;
}

} // namespace Mcp
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
