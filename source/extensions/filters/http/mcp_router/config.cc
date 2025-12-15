#include "source/extensions/filters/http/mcp_router/config.h"

#include "envoy/extensions/filters/http/mcp_router/v3/mcp_router.pb.h"
#include "envoy/registry/registry.h"

#include "source/extensions/filters/http/mcp_router/mcp_router.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace McpRouter {

Http::FilterFactoryCb McpRouterFilterConfigFactory::createFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::http::mcp_router::v3::McpRouter& proto_config,
    const std::string& /* stats_prefix */, Server::Configuration::FactoryContext& context) {

  auto config = std::make_shared<McpRouterConfig>(proto_config, context);

  return [config](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamDecoderFilter(std::make_shared<McpRouterFilter>(config));
  };
}

/**
 * Static registration for the MCP router filter. @see RegisterFactory.
 */
REGISTER_FACTORY(McpRouterFilterConfigFactory, Server::Configuration::NamedHttpFilterConfigFactory);

} // namespace McpRouter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
