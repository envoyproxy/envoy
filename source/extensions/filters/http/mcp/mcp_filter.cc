#include "source/extensions/filters/http/mcp/mcp_filter.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>

#include "envoy/buffer/buffer.h"
#include "envoy/http/codes.h"
#include "envoy/http/filter.h"
#include "envoy/http/header_map.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/stats_macros.h"
#include "envoy/stream_info/filter_state.h"

#include "source/common/common/logger.h"
#include "source/common/http/headers.h"
#include "source/common/http/utility.h"
#include "source/common/protobuf/protobuf.h"
#include "source/common/tracing/tracing_validation.h"
#include "source/extensions/filters/common/mcp/constants.h"
#include "source/extensions/filters/common/mcp/filter_state.h"
#include "source/extensions/filters/http/mcp/mcp_json_parser.h"

#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/types/optional.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Mcp {

using FilterStateObject = Filters::Common::Mcp::FilterStateObject;

namespace {

const Http::LowerCaseString kMcpSessionId{
    std::string(Filters::Common::Mcp::McpConstants::MCP_SESSION_ID_HEADER)};

McpFilterStats generateStats(const std::string& prefix, Stats::Scope& scope) {
  const std::string final_prefix = absl::StrCat(prefix, "mcp.");
  return McpFilterStats{MCP_FILTER_STATS(POOL_COUNTER_PREFIX(scope, final_prefix))};
}

const Http::LowerCaseString& traceparentHeader() {
  CONSTRUCT_ON_FIRST_USE(Http::LowerCaseString, "traceparent");
}

const Http::LowerCaseString& tracestateHeader() {
  CONSTRUCT_ON_FIRST_USE(Http::LowerCaseString, "tracestate");
}

const Http::LowerCaseString& baggageHeader() {
  CONSTRUCT_ON_FIRST_USE(Http::LowerCaseString, "baggage");
}

void injectTraceContext(const Protobuf::Map<std::string, Protobuf::Value>& meta_fields,
                        Http::RequestHeaderMap& headers) {
  const auto& tp_it = meta_fields.find("traceparent");
  if (tp_it == meta_fields.end() || tp_it->second.kind_case() != Protobuf::Value::kStringValue) {
    return;
  }

  const std::string& tp = tp_it->second.string_value();
  if (!Envoy::Tracing::isValidTraceParent(tp)) {
    return;
  }

  headers.remove(traceparentHeader());
  headers.remove(tracestateHeader());

  headers.setCopy(traceparentHeader(), tp);

  const auto& ts_it = meta_fields.find("tracestate");
  if (ts_it != meta_fields.end() && ts_it->second.kind_case() == Protobuf::Value::kStringValue) {
    const std::string& ts = ts_it->second.string_value();
    if (Envoy::Tracing::isValidTraceState(ts)) {
      headers.setCopy(tracestateHeader(), ts);
    }
  }
}

void injectBaggage(const Protobuf::Map<std::string, Protobuf::Value>& meta_fields,
                   Http::RequestHeaderMap& headers) {
  const auto& bg_it = meta_fields.find("baggage");
  if (bg_it == meta_fields.end() || bg_it->second.kind_case() != Protobuf::Value::kStringValue) {
    return;
  }

  const std::string& bg = bg_it->second.string_value();
  if (Envoy::Tracing::isValidBaggage(bg)) {
    headers.setCopy(baggageHeader(), bg);
  }
}
} // namespace

McpFilterConfig::McpFilterConfig(const envoy::extensions::filters::http::mcp::v3::Mcp& proto_config,
                                 const std::string& stats_prefix, Stats::Scope& scope)
    : traffic_mode_(proto_config.traffic_mode()),
      clear_route_cache_(proto_config.clear_route_cache()),
      propagate_trace_context_(proto_config.has_propagate_trace_context()
                                   ? absl::make_optional(proto_config.propagate_trace_context())
                                   : absl::nullopt),
      propagate_baggage_(proto_config.has_propagate_baggage()
                             ? absl::make_optional(proto_config.propagate_baggage())
                             : absl::nullopt),
      max_request_body_size_(proto_config.has_max_request_body_size()
                                 ? proto_config.max_request_body_size().value()
                                 : 8192), // Default: 8KB
      request_storage_mode_(proto_config.request_storage_mode()),
      metadata_namespace_(Filters::Common::Mcp::metadataNamespace()),
      parser_config_(proto_config.has_parser_config()
                         ? McpParserConfig::fromProto(proto_config.parser_config())
                         : McpParserConfig::createDefault()),
      stats_(generateStats(stats_prefix, scope)) {}

bool McpFilter::isValidMcpDeleteRequest(const Http::RequestHeaderMap& headers) const {
  // DELETE is only meaningful for MCP session termination when MCP-Session-Id is present.
  if (headers.getMethodValue() != Http::Headers::get().MethodValues.Delete) {
    return false;
  }
  return !headers.get(kMcpSessionId).empty();
}

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
  // Check if this is a POST request with JSON content.
  // Content-Type is JSON if it is exactly "application/json" or starts with
  // "application/json" followed by ';' or ' ' (for parameters like charset).
  // This rejects related but distinct types like application/json-patch+json.
  const absl::string_view content_type = headers.getContentTypeValue();
  const auto& json_ct = Http::Headers::get().ContentTypeValues.Json;
  bool is_json_content_type =
      absl::StartsWith(content_type, json_ct) &&
      (content_type.size() == json_ct.size() || content_type[json_ct.size()] == ';' ||
       content_type[json_ct.size()] == ' ');
  bool is_post_request =
      headers.getMethodValue() == Http::Headers::get().MethodValues.Post && is_json_content_type;

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
  if (isValidMcpDeleteRequest(headers)) {
    is_mcp_request_ = true;
    ENVOY_LOG(debug, "valid MCP DELETE session-termination request, passing through");
    return Http::FilterHeadersStatus::Continue;
  }

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

      // Set the buffer limit.
      const uint32_t max_size = getMaxRequestBodySize();
      if (max_size > 0) {
        decoder_callbacks_->setBufferLimit(max_size);
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

  const size_t chunk_size = data.length();

  ENVOY_LOG(trace, "decodeData: chunk_size={}, total_parsed={}, end_stream={}", chunk_size,
            bytes_parsed_, end_stream);

  const uint32_t max_size = getMaxRequestBodySize();

  size_t to_parse = chunk_size;
  if (max_size > 0) {
    size_t remaining_limit = max_size - bytes_parsed_;
    to_parse = std::min(chunk_size, remaining_limit);
  }

  const char* linearized = static_cast<const char*>(data.linearize(to_parse));
  absl::string_view parse_view(linearized, to_parse);

  // The partial parser will return an OK status if the requirements are not satisfied.
  // It will potentially be a bad status due to the partial parse if all the requirements
  // are extracted.
  auto status = parser_->parse(parse_view);
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
      if (size_limit_hit && parser_->hasOptionalFields() && parser_->hasAllRequiredFields()) {
        ENVOY_LOG(debug, "size limit hit before optional fields; proceeding with partial parse");
        return completeParsing();
      }
      config_->stats().body_too_large_.inc();
      handleParseError("reached end_stream or configured body size, don't get enough data.");
      return Http::FilterDataStatus::StopIterationNoBuffer;
    }
    return completeParsing();
  }

  return Http::FilterDataStatus::StopIterationAndWatermark;
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

  Protobuf::Struct metadata = parser_->metadata();

  const std::string& group_metadata_key = config_->parserConfig().groupMetadataKey();
  if (!group_metadata_key.empty()) {
    std::string method_group = config_->parserConfig().getMethodGroup(parser_->getMethod());
    (*metadata.mutable_fields())[group_metadata_key].set_string_value(method_group);
    ENVOY_LOG(debug, "MCP filter set method group: {}={}", group_metadata_key, method_group);
  }

  // Handle tracing field extraction and header injection.
  if (config_->propagateTraceContext().has_value() || config_->propagateBaggage().has_value()) {
    const Protobuf::Value* meta_value = parser_->getNestedValue(
        std::string(Filters::Common::Mcp::McpConstants::Paths::PARAMS_META));
    auto headers = decoder_callbacks_->requestHeaders();
    if (meta_value != nullptr && meta_value->has_struct_value() && headers.has_value()) {
      const auto& meta_fields = meta_value->struct_value().fields();
      if (config_->propagateTraceContext().has_value()) {
        injectTraceContext(meta_fields, *headers);
      }
      if (config_->propagateBaggage().has_value()) {
        injectBaggage(meta_fields, *headers);
      }
    }
  }

  if (!metadata.fields().empty()) {
    if (config_->shouldStoreToFilterState()) {
      auto filter_state_obj =
          std::make_shared<FilterStateObject>(parser_->getMethod(), metadata, is_mcp_request_);
      decoder_callbacks_->streamInfo().filterState()->setData(
          std::string(FilterStateObject::FilterStateKey), std::move(filter_state_obj),
          StreamInfo::FilterState::StateType::ReadOnly, StreamInfo::FilterState::LifeSpan::Request,
          StreamInfo::StreamSharingMayImpactPooling::None);
    }

    if (config_->shouldStoreToDynamicMetadata()) {
      (*metadata.mutable_fields())[std::string(Filters::Common::Mcp::McpConstants::IS_MCP_REQUEST)]
          .set_bool_value(is_mcp_request_);
      decoder_callbacks_->streamInfo().setDynamicMetadata(config_->metadataNamespace(), metadata);
      ENVOY_LOG(debug, "MCP filter set dynamic metadata: {}", metadata.DebugString());
    }

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
