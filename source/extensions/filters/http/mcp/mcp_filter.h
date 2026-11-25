#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <string>

#include "envoy/buffer/buffer.h"
#include "envoy/common/pure.h"
#include "envoy/extensions/filters/http/mcp/v3/mcp.pb.h"
#include "envoy/http/codes.h"
#include "envoy/http/filter.h"
#include "envoy/http/header_map.h"
#include "envoy/server/filter_config.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/stats_macros.h"
#include "envoy/stream_info/stream_info.h"

#include "source/common/common/logger.h"
#include "source/common/protobuf/protobuf.h"
#include "source/common/protobuf/utility.h"
#include "source/extensions/filters/common/mcp/constants.h"
#include "source/extensions/filters/common/mcp/filter_state.h"
#include "source/extensions/filters/http/common/pass_through_filter.h"
#include "source/extensions/filters/http/mcp/mcp_json_parser.h"

#include "absl/strings/string_view.h"
#include "nlohmann/json_fwd.hpp"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Mcp {

/**
 * All MCP filter stats. @see stats_macros.h
 */
#define MCP_FILTER_STATS(COUNTER)                                                                  \
  COUNTER(requests_rejected)                                                                       \
  COUNTER(invalid_json)                                                                            \
  COUNTER(body_too_large)                                                                          \
  COUNTER(duplicate_keys_rejected)                                                                 \
  COUNTER(header_mismatch)

/**
 * Struct definition for MCP filter stats. @see stats_macros.h
 */
struct McpFilterStats {
  MCP_FILTER_STATS(GENERATE_COUNTER_STRUCT)
};

/**
 * Configuration for the MCP filter.
 */
class McpFilterConfig {
public:
  McpFilterConfig(const envoy::extensions::filters::http::mcp::v3::Mcp& proto_config,
                  const std::string& stats_prefix, Stats::Scope& scope);

  envoy::extensions::filters::http::mcp::v3::Mcp::TrafficMode trafficMode() const {
    return traffic_mode_;
  }

  bool shouldRejectNonMcp() const {
    return traffic_mode_ == envoy::extensions::filters::http::mcp::v3::Mcp::REJECT_NO_MCP;
  }

  bool clearRouteCache() const { return clear_route_cache_; }

  const std::optional<
      envoy::extensions::filters::http::mcp::v3::Mcp::TraceContextPropagationConfig>&
  propagateTraceContext() const {
    return propagate_trace_context_;
  }
  const std::optional<envoy::extensions::filters::http::mcp::v3::Mcp::BaggagePropagationConfig>&
  propagateBaggage() const {
    return propagate_baggage_;
  }

  bool rejectDuplicateKeys() const { return parser_config_.rejectDuplicateKeys(); }
  uint32_t maxRequestBodySize() const { return max_request_body_size_; }
  envoy::extensions::filters::http::mcp::v3::Mcp::AttributeSource attributeSource() const {
    return attribute_source_;
  }
  bool earlyTerminateWhenRoutable() const { return early_terminate_when_routable_; }

  bool isProtocolVersionSupported(absl::string_view version) const;

  const ParserConfig& parserConfig() const { return parser_config_; }
  bool shouldStoreToDynamicMetadata() const {
    return request_storage_mode_ ==
               envoy::extensions::filters::http::mcp::v3::Mcp::MODE_UNSPECIFIED ||
           request_storage_mode_ ==
               envoy::extensions::filters::http::mcp::v3::Mcp::DYNAMIC_METADATA ||
           request_storage_mode_ ==
               envoy::extensions::filters::http::mcp::v3::Mcp::DYNAMIC_METADATA_AND_FILTER_STATE;
  }
  bool shouldStoreToFilterState() const {
    return request_storage_mode_ == envoy::extensions::filters::http::mcp::v3::Mcp::FILTER_STATE ||
           request_storage_mode_ ==
               envoy::extensions::filters::http::mcp::v3::Mcp::DYNAMIC_METADATA_AND_FILTER_STATE;
  }
  const std::string& metadataNamespace() const { return metadata_namespace_; }

  McpFilterStats& stats() { return stats_; }

  // The body size limit `proto_config` sets; 0 for no limit.
  static uint32_t
  maxRequestBodySize(const envoy::extensions::filters::http::mcp::v3::Mcp& proto_config);

private:
  const envoy::extensions::filters::http::mcp::v3::Mcp::TrafficMode traffic_mode_;
  const bool clear_route_cache_;
  const std::optional<envoy::extensions::filters::http::mcp::v3::Mcp::TraceContextPropagationConfig>
      propagate_trace_context_;
  const std::optional<envoy::extensions::filters::http::mcp::v3::Mcp::BaggagePropagationConfig>
      propagate_baggage_;
  const uint32_t max_request_body_size_;
  const envoy::extensions::filters::http::mcp::v3::Mcp::RequestStorageMode request_storage_mode_;
  const envoy::extensions::filters::http::mcp::v3::Mcp::AttributeSource attribute_source_;

  const bool early_terminate_when_routable_;
  const std::optional<std::string> max_supported_protocol_version_;
  const std::string metadata_namespace_;
  ParserConfig parser_config_;
  McpFilterStats stats_;
};

/**
 * Per-route configuration for the MCP filter.
 */
class McpOverrideConfig : public Router::RouteSpecificFilterConfig {
public:
  explicit McpOverrideConfig(
      const envoy::extensions::filters::http::mcp::v3::McpOverride& proto_config)
      : traffic_mode_(proto_config.traffic_mode()),
        max_request_body_size_(
            proto_config.has_max_request_body_size()
                ? std::optional<uint32_t>(proto_config.max_request_body_size().value())
                : std::nullopt),
        clear_route_cache_(
            boolOverride(PROTOBUF_GET_OPTIONAL_WRAPPED(proto_config, clear_route_cache_override),
                         proto_config.clear_route_cache())),
        parser_config_(
            proto_config.has_parser_config()
                ? std::make_optional(McpParserConfig::fromProto(proto_config.parser_config()))
                : std::nullopt),
        request_storage_mode_(proto_config.request_storage_mode()),
        reject_duplicate_keys_(boolOverride(
            PROTOBUF_GET_OPTIONAL_WRAPPED(proto_config, reject_duplicate_keys_override),
            proto_config.reject_duplicate_keys())) {}

  envoy::extensions::filters::http::mcp::v3::Mcp::TrafficMode trafficMode() const {
    return traffic_mode_;
  }

  std::optional<uint32_t> maxRequestBodySize() const { return max_request_body_size_; }
  std::optional<bool> clearRouteCache() const { return clear_route_cache_; }
  const std::optional<ParserConfig>& parserConfig() const { return parser_config_; }
  envoy::extensions::filters::http::mcp::v3::Mcp::RequestStorageMode requestStorageMode() const {
    return request_storage_mode_;
  }
  std::optional<bool> rejectDuplicateKeys() const { return reject_duplicate_keys_; }

private:
  // The deprecated bool fields cannot express "unset", so they only apply when true.
  static std::optional<bool> boolOverride(std::optional<bool> override_value,
                                          bool deprecated_value) {
    if (override_value.has_value()) {
      return override_value;
    }
    return deprecated_value ? std::make_optional(true) : std::nullopt;
  }

  const envoy::extensions::filters::http::mcp::v3::Mcp::TrafficMode traffic_mode_;
  const std::optional<uint32_t> max_request_body_size_;
  const std::optional<bool> clear_route_cache_;
  const std::optional<ParserConfig> parser_config_;
  const envoy::extensions::filters::http::mcp::v3::Mcp::RequestStorageMode request_storage_mode_;
  const std::optional<bool> reject_duplicate_keys_;
};

using McpFilterConfigSharedPtr = std::shared_ptr<McpFilterConfig>;

/**
 * What an McpInspector needs from the filter that hosts it.
 */
class McpInspectorCallbacks {
public:
  virtual ~McpInspectorCallbacks() = default;

  virtual StreamInfo::StreamInfo& streamInfo() PURE;
  virtual Http::RequestHeaderMapOptRef requestHeaders() PURE;
  // The route's override; null when the route has none.
  virtual const McpOverrideConfig* routeOverride() const PURE;
  virtual void clearRouteCache() PURE;
  virtual void sendLocalReply(Http::Code code, absl::string_view body,
                              std::function<void(Http::ResponseHeaderMap&)> modify_headers,
                              absl::string_view details) PURE;
};

/**
 * Inspects one request: classifies it as MCP traffic, holds it to the traffic mode, and publishes
 * its attributes. Shared by the MCP HTTP filter and the MCP AI filter, which differ only in how
 * the request body reaches it.
 */
class McpInspector : public Logger::Loggable<Logger::Id::mcp> {
public:
  enum class Result {
    // The request goes on.
    Continue,
    // The body, or more of it, is needed.
    NeedBody,
    // The request was answered with a local reply.
    Replied,
  };

  McpInspector(McpFilterConfigSharedPtr config, McpInspectorCallbacks& callbacks)
      : config_(std::move(config)), callbacks_(callbacks) {}

  Result onRequestHeaders(const Http::RequestHeaderMap& headers, bool end_stream);

  // Parses the body as it arrives, up to maxRequestBodySize().
  Result onRequestBody(const Buffer::Instance& data, bool end_stream);

  // Takes a body of `body_size` bytes that the host parsed whole, once onRequestHeaders() asked
  // for it. A body the streaming parse would stop short in, or one the host left out a value the
  // inspector reads, is handled as one parsed in part.
  Result onRequestBody(const nlohmann::json& body, uint64_t body_size);

  // Takes a body that is not JSON, once onRequestHeaders() asked for it.
  Result onInvalidRequestBody();

  uint32_t maxRequestBodySize() const;

private:
  bool isValidMcpSseRequest(const Http::RequestHeaderMap& headers) const;
  bool isValidMcpPostRequest(const Http::RequestHeaderMap& headers) const;
  bool isValidMcpDeleteRequest(const Http::RequestHeaderMap& headers) const;
  bool shouldRejectRequest();
  // Traffic mode after applying any per-route override. Latched on first access in
  // onRequestHeaders; we assume the route does not change during the request, so it is resolved
  // only once.
  envoy::extensions::filters::http::mcp::v3::Mcp::TrafficMode trafficMode();
  bool clearRouteCache() const;
  const ParserConfig& parserConfig() const;
  bool shouldStoreToDynamicMetadata() const;
  bool shouldStoreToFilterState() const;
  bool rejectDuplicateKeys() const;
  // Whether buffering may stop once routing attributes are collected (see .cc
  // for gating). Non-const because it resolves the latched traffic mode.
  bool canEarlyTerminate();
  // Whether onRequestHeaders() asked for the body.
  bool awaitsBody() const;
  // Rejects a body parsed in part, or goes on with what was read, by the traffic mode.
  Result onIncompleteBody(Filters::Common::Mcp::Status reason);

  void recordErrorState(absl::string_view error_msg, Filters::Common::Mcp::Status status);
  void sendErrorReply(absl::string_view error_msg, Filters::Common::Mcp::Status status);
  void sendUnsupportedProtocolVersionReply(absl::string_view requested_version);
  void sendJsonRpcErrorReply(int error_code, absl::string_view error_msg);
  void sendHeaderMismatchReply(absl::string_view error_msg);
  void sendMethodNotAllowedReply(absl::string_view error_msg);
  bool needsBody();
  bool hasCompleteHeaderAttributes() const;
  bool headerAttributesMatch() const;
  bool verifyHeaderAttributes() const;
  bool hasRequiredClientCapabilities() const;
  enum class ProtocolVersionValidationResult {
    Ok,
    Missing,
    Invalid,
    Mismatch,
  };
  ProtocolVersionValidationResult validateProtocolVersion() const;
  Result completeParsing();
  void setDynamicMetadataStatus(Protobuf::Struct metadata);
  void populateMetadataFromHeaders();
  bool shouldUseNewSpecSemantics() const;

  McpFilterConfigSharedPtr config_;
  McpInspectorCallbacks& callbacks_;
  std::optional<envoy::extensions::filters::http::mcp::v3::Mcp::TrafficMode> traffic_mode_;
  uint32_t bytes_parsed_{0};
  bool parsing_complete_{false};
  bool is_exceeding_limit_{false};
  std::unique_ptr<JsonPathParser> parser_;
  bool is_mcp_request_{false};
  bool is_json_post_request_{false};
  bool skip_body_parsing_{false};
  bool use_new_spec_semantics_{false};
  std::string header_method_;
  std::string header_name_;
  std::optional<std::string> protocol_version_;
  Filters::Common::Mcp::Status status_{Filters::Common::Mcp::Status::Ok};
  std::optional<Filters::Common::Mcp::Status> passthrough_reason_;
};

/**
 * MCP proxy implementation.
 */
class McpFilter : public Http::PassThroughFilter,
                  public McpInspectorCallbacks,
                  public Logger::Loggable<Logger::Id::mcp> {
public:
  explicit McpFilter(McpFilterConfigSharedPtr config) : inspector_(std::move(config), *this) {}

  // Http::StreamDecoderFilter
  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap& headers,
                                          bool end_stream) override;
  Http::FilterDataStatus decodeData(Buffer::Instance& data, bool end_stream) override;

  void setDecoderFilterCallbacks(Http::StreamDecoderFilterCallbacks& callbacks) override {
    decoder_callbacks_ = &callbacks;
  }

  // McpInspectorCallbacks
  StreamInfo::StreamInfo& streamInfo() override { return decoder_callbacks_->streamInfo(); }
  Http::RequestHeaderMapOptRef requestHeaders() override {
    return decoder_callbacks_->requestHeaders();
  }
  const McpOverrideConfig* routeOverride() const override;
  void clearRouteCache() override;
  void sendLocalReply(Http::Code code, absl::string_view body,
                      std::function<void(Http::ResponseHeaderMap&)> modify_headers,
                      absl::string_view details) override;

private:
  Http::StreamDecoderFilterCallbacks* decoder_callbacks_{};
  McpInspector inspector_;
};

} // namespace Mcp
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
