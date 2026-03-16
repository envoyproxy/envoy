#include "source/extensions/filters/http/mcp_json_rest_bridge/filter_config.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace McpJsonRestBridge {

McpJsonRestBridgeFilterConfig::McpJsonRestBridgeFilterConfig(
    const envoy::extensions::filters::http::mcp_json_rest_bridge::v3::McpJsonRestBridge&
        proto_config)
    : proto_config_(proto_config) {
  for (const auto& tool : proto_config.tool_config().tools()) {
    tool_to_http_rule_[tool.name()] = tool.http_rule();
  }
  ENVOY_LOG(debug, "Received MCP JSON REST Bridge config: {}", proto_config_.DebugString());
}

absl::StatusOr<envoy::extensions::filters::http::mcp_json_rest_bridge::v3::HttpRule>
McpJsonRestBridgeFilterConfig::getHttpRule(absl::string_view tool_name) const {
  auto it = tool_to_http_rule_.find(tool_name);
  if (it == tool_to_http_rule_.end()) {
    return absl::InvalidArgumentError(
        fmt::format("Failed to find http rule for tool_name: {}", tool_name));
  }
  return it->second;
}

} // namespace McpJsonRestBridge
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
