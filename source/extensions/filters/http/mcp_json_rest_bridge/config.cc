#include "source/extensions/filters/http/mcp_json_rest_bridge/config.h"

#include "envoy/extensions/filters/http/mcp_json_rest_bridge/v3/mcp_json_rest_bridge.pb.h"
#include "envoy/registry/registry.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace McpJsonRestBridge {

Http::FilterFactoryCb McpJsonRestBridgeFilterConfigFactory::createFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::http::mcp_json_rest_bridge::v3::McpJsonRestBridge&,
    const std::string&, Server::Configuration::FactoryContext&) {

  return [](Http::FilterChainFactoryCallbacks&) -> void {};
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
