#pragma once

#include <memory>
#include <string>

#include "envoy/extensions/filters/http/mcp/v3/mcp.pb.h"
#include "envoy/http/filter.h"
#include "envoy/server/filter_config.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/stats_macros.h"

#include "source/common/common/logger.h"
#include "source/common/protobuf/protobuf.h"
#include "source/extensions/filters/http/common/pass_through_filter.h"
#include "source/extensions/filters/http/mcp/mcp_json_parser.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Mcp {

namespace MetadataKeys {
// Core MCP fields
constexpr absl::string_view FilterName = "mcp_proxy";
} // namespace MetadataKeys

/**
 * All MCP filter stats. @see stats_macros.h
 */
#define MCP_FILTER_STATS(COUNTER)                                                                  \
  COUNTER(requests_rejected)                                                                       \
  COUNTER(invalid_json)                                                                            \
  COUNTER(body_too_large)

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

  uint32_t maxRequestBodySize() const { return max_request_body_size_; }
  const ParserConfig& parserConfig() const { return parser_config_; }

  McpFilterStats& stats() { return stats_; }

private:
  const envoy::extensions::filters::http::mcp::v3::Mcp::TrafficMode traffic_mode_;
  const bool clear_route_cache_;
  const uint32_t max_request_body_size_;
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
                ? absl::optional<uint32_t>(proto_config.max_request_body_size().value())
                : absl::nullopt) {}

  envoy::extensions::filters::http::mcp::v3::Mcp::TrafficMode trafficMode() const {
    return traffic_mode_;
  }

  absl::optional<uint32_t> maxRequestBodySize() const { return max_request_body_size_; }

private:
  const envoy::extensions::filters::http::mcp::v3::Mcp::TrafficMode traffic_mode_;
  const absl::optional<uint32_t> max_request_body_size_;
};

using McpFilterConfigSharedPtr = std::shared_ptr<McpFilterConfig>;

/**
 * MCP proxy implementation.
 */
class McpFilter : public Http::PassThroughFilter, public Logger::Loggable<Logger::Id::mcp> {
public:
  explicit McpFilter(McpFilterConfigSharedPtr config) : config_(config) {}

  // Http::StreamDecoderFilter
  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap& headers,
                                          bool end_stream) override;
  Http::FilterDataStatus decodeData(Buffer::Instance& data, bool end_stream) override;

  void setDecoderFilterCallbacks(Http::StreamDecoderFilterCallbacks& callbacks) override {
    decoder_callbacks_ = &callbacks;
  }

private:
  bool isValidMcpSseRequest(const Http::RequestHeaderMap& headers) const;
  bool isValidMcpPostRequest(const Http::RequestHeaderMap& headers) const;
  bool shouldRejectRequest() const;
  uint32_t getMaxRequestBodySize() const;

  void handleParseError(absl::string_view error_msg);
  Http::FilterDataStatus completeParsing();

  McpFilterConfigSharedPtr config_;
  Http::StreamDecoderFilterCallbacks* decoder_callbacks_{};
  uint32_t bytes_parsed_{0};
  bool parsing_complete_{false};
  std::unique_ptr<JsonPathParser> parser_;
  bool is_mcp_request_{false};
  bool is_json_post_request_{false};
};

} // namespace Mcp
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
