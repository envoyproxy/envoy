#include "source/extensions/filters/http/mcp_json_rest_bridge/config.h"

#include "envoy/extensions/filters/http/mcp_json_rest_bridge/v3/mcp_json_rest_bridge.pb.h"
#include "envoy/registry/registry.h"

#include "source/extensions/filters/http/mcp_json_rest_bridge/mcp_json_rest_bridge_filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace McpJsonRestBridge {

absl::StatusOr<Http::FilterFactoryCb>
McpJsonRestBridgeFilterConfigFactory::createFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::http::mcp_json_rest_bridge::v3::McpJsonRestBridge&
        proto_config,
    const std::string&, Server::Configuration::FactoryContext&) {

  if (proto_config.tool_config().has_tool_list_http_rule()) {
    const auto& rule = proto_config.tool_config().tool_list_http_rule();
    if (rule.get().empty() || !rule.put().empty() || !rule.post().empty() ||
        !rule.delete_().empty() || !rule.patch().empty() || !rule.body().empty()) {
      return absl::InvalidArgumentError(
          "tool_list_http_rule must be a GET request with an empty body");
    }
  }

  auto config = std::make_shared<McpJsonRestBridgeFilterConfig>(proto_config);

  return [config](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamFilter(std::make_shared<McpJsonRestBridgeFilter>(config));
  };
}

/**
 * Static registration for the MCP JSON REST bridge filter. @see RegisterFactory.
 */
REGISTER_FACTORY(McpJsonRestBridgeFilterConfigFactory,
                 Server::Configuration::NamedHttpFilterConfigFactory);

} // namespace McpJsonRestBridge
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
