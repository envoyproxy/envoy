#include "source/extensions/filters/http/mcp_json_rest_bridge/config.h"

#include "envoy/extensions/filters/http/mcp_json_rest_bridge/v3/mcp_json_rest_bridge.pb.h"
#include "envoy/registry/registry.h"

#include "source/extensions/filters/http/mcp_json_rest_bridge/mcp_json_rest_bridge_filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace McpJsonRestBridge {

Http::FilterFactoryCb McpJsonRestBridgeFilterConfigFactory::createFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::http::mcp_json_rest_bridge::v3::McpJsonRestBridge&
        proto_config,
    const std::string&, Server::Configuration::FactoryContext&) {

  auto config = std::make_shared<McpJsonRestBridgeFilterConfig>(proto_config);

  return [config](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamFilter(std::make_shared<McpJsonRestBridgeFilter>(config));
  };
}

// TODO(guoyilin): Read route-specific config (perFilterConfig/mostSpecificPerFilterConfig) in the
// filter.
absl::StatusOr<Router::RouteSpecificFilterConfigConstSharedPtr>
McpJsonRestBridgeFilterConfigFactory::createRouteSpecificFilterConfigTyped(
    const envoy::extensions::filters::http::mcp_json_rest_bridge::v3::McpJsonRestBridge&
        proto_config,
    Server::Configuration::ServerFactoryContext&, ProtobufMessage::ValidationVisitor&) {
  return std::make_shared<McpJsonRestBridgeFilterConfig>(proto_config);
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
