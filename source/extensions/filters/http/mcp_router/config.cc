#include "source/extensions/filters/http/mcp_router/config.h"

#include "envoy/extensions/filters/http/mcp_router/v3/mcp_router.pb.h"
#include "envoy/registry/registry.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace McpRouter {

Http::FilterFactoryCb McpRouterFilterConfigFactory::createFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::http::mcp_router::v3::McpRouter&, const std::string&,
    Server::Configuration::FactoryContext&) {

  return [](Http::FilterChainFactoryCallbacks&) -> void {};
}

/**
 * Static registration for the MCP router filter. @see RegisterFactory.
 */
REGISTER_FACTORY(McpRouterFilterConfigFactory, Server::Configuration::NamedHttpFilterConfigFactory);

} // namespace McpRouter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
