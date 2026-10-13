#include "source/extensions/filters/http/mcp_json_rest_bridge/config.h"

#include "envoy/extensions/filters/http/mcp_json_rest_bridge/v3/mcp_json_rest_bridge.pb.h"
#include "envoy/registry/registry.h"

#include "source/extensions/filters/http/mcp_json_rest_bridge/mcp_json_rest_bridge_filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace McpJsonRestBridge {

absl::StatusOr<Http::FilterFactoryCb>
McpJsonRestBridgeFilterConfigFactory::createHttpFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::http::mcp_json_rest_bridge::v3::McpJsonRestBridge&
        proto_config,
    Server::Configuration::ServerFactoryContext&, Server::Configuration::ExtraFactoryContext&) {

  auto config_or = McpJsonRestBridgeFilterConfig::create(proto_config);
  if (!config_or.ok()) {
    return config_or.status();
  }
  auto config = config_or.value();

  return [config](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamFilter(std::make_shared<McpJsonRestBridgeFilter>(config));
  };
}

absl::StatusOr<Router::RouteSpecificFilterConfigConstSharedPtr>
McpJsonRestBridgeFilterConfigFactory::createRouteSpecificFilterConfigTyped(
    const envoy::extensions::filters::http::mcp_json_rest_bridge::v3::McpJsonRestBridgePerRoute&
        proto_config,
    Server::Configuration::ServerFactoryContext&, ProtobufMessage::ValidationVisitor&) {
  auto config_or = McpJsonRestBridgePerRouteConfig::create(proto_config);
  if (!config_or.ok()) {
    return config_or.status();
  }
  return config_or.value();
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
