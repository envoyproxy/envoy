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

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Mcp {

namespace MetadataKeys {
// Core MCP fields
constexpr absl::string_view FilterName = "mcp_proxy";
} // namespace MetadataKeys

// MCP protocol constants
namespace McpConstants {
constexpr absl::string_view JsonRpcVersion = "2.0";
} // namespace McpConstants

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

  McpFilterStats& stats() { return stats_; }

private:
  const envoy::extensions::filters::http::mcp::v3::Mcp::TrafficMode traffic_mode_;
  const bool clear_route_cache_;
  const uint32_t max_request_body_size_;
  McpFilterStats stats_;
};

/**
 * Per-route configuration for the MCP filter.
 */
class McpOverrideConfig : public Router::RouteSpecificFilterConfig {
public:
  explicit McpOverrideConfig(
      const envoy::extensions::filters::http::mcp::v3::McpOverride& proto_config)
      : traffic_mode_(proto_config.traffic_mode()) {}

  envoy::extensions::filters::http::mcp::v3::Mcp::TrafficMode trafficMode() const {
    return traffic_mode_;
  }

private:
  const envoy::extensions::filters::http::mcp::v3::Mcp::TrafficMode traffic_mode_;
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

  // Http::StreamEncoderFilter
  Http::FilterHeadersStatus encodeHeaders(Http::ResponseHeaderMap&, bool) override {
    return Http::FilterHeadersStatus::Continue;
  };
  Http::FilterDataStatus encodeData(Buffer::Instance&, bool) override {
    return Http::FilterDataStatus::Continue;
  };

  void setDecoderFilterCallbacks(Http::StreamDecoderFilterCallbacks& callbacks) override {
    decoder_callbacks_ = &callbacks;
  }

private:
  bool isValidMcpSseRequest(const Http::RequestHeaderMap& headers) const;
  bool isValidMcpPostRequest(const Http::RequestHeaderMap& headers) const;
  bool shouldRejectRequest() const;

  void finalizeDynamicMetadata();
  McpFilterConfigSharedPtr config_;
  Http::StreamDecoderFilterCallbacks* decoder_callbacks_{};
  bool is_mcp_request_{false};
  bool is_json_post_request_{false};
  std::unique_ptr<Protobuf::Struct> metadata_;
};

} // namespace Mcp
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
