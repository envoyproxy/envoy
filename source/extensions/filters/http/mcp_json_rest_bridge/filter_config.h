#pragma once

#include <memory>

#include "envoy/extensions/filters/http/mcp_json_rest_bridge/v3/mcp_json_rest_bridge.pb.h"
#include "envoy/router/router.h"

#include "source/common/common/logger.h"

#include "absl/container/flat_hash_map.h"
#include "absl/status/statusor.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace McpJsonRestBridge {

/**
 * Configuration for the MCP JSON REST Bridge filter.
 */
class McpJsonRestBridgeFilterConfig : public Router::RouteSpecificFilterConfig,
                                      public Logger::Loggable<Logger::Id::config> {
public:
  explicit McpJsonRestBridgeFilterConfig(
      const envoy::extensions::filters::http::mcp_json_rest_bridge::v3::McpJsonRestBridge&
          proto_config);

  absl::StatusOr<envoy::extensions::filters::http::mcp_json_rest_bridge::v3::HttpRule>
  getHttpRule(absl::string_view tool_name) const;

private:
  absl::flat_hash_map<std::string,
                      envoy::extensions::filters::http::mcp_json_rest_bridge::v3::HttpRule>
      tool_to_http_rule_;
  envoy::extensions::filters::http::mcp_json_rest_bridge::v3::McpJsonRestBridge proto_config_;
};

using McpJsonRestBridgeFilterConfigSharedPtr = std::shared_ptr<McpJsonRestBridgeFilterConfig>;

} // namespace McpJsonRestBridge
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
