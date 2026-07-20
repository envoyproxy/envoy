#include "source/extensions/filters/http/mcp_router/config.h"

#include "envoy/extensions/filters/http/mcp_router/v3/mcp_router.pb.h"
#include "envoy/registry/registry.h"

#include "source/extensions/filters/http/mcp_router/mcp_router.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace McpRouter {

absl::StatusOr<Http::FilterFactoryCb> McpRouterFilterConfigFactory::createFilterFactory(
    const envoy::extensions::filters::http::mcp_router::v3::McpRouter& proto_config,
    const std::string& stats_prefix, Server::Configuration::ServerFactoryContext& context,
    Stats::Scope& scope) {

  auto config = std::make_shared<McpRouterConfigImpl>(proto_config, stats_prefix, scope, context);

  return [config](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamDecoderFilter(std::make_shared<McpRouterFilter>(config));
  };
}

absl::StatusOr<Http::FilterFactoryCb>
McpRouterFilterConfigFactory::createFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::http::mcp_router::v3::McpRouter& proto_config,
    const std::string& stats_prefix, Server::Configuration::FactoryContext& context) {
  return createFilterFactory(proto_config, stats_prefix, context.serverFactoryContext(),
                             context.scope());
}

absl::StatusOr<Http::FilterFactoryCb>
McpRouterFilterConfigFactory::createHttpFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::http::mcp_router::v3::McpRouter& proto_config,
    const std::string& stats_prefix, Server::Configuration::ServerFactoryContext& context) {
  return createFilterFactory(proto_config, stats_prefix, context, context.scope());
}

/**
 * Static registration for the MCP router filter. @see RegisterFactory.
 */
REGISTER_FACTORY(McpRouterFilterConfigFactory, Server::Configuration::NamedHttpFilterConfigFactory);

} // namespace McpRouter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
