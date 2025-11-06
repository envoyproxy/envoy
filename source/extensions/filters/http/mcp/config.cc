#include "source/extensions/filters/http/mcp/config.h"

#include "envoy/registry/registry.h"

#include "source/extensions/filters/http/mcp/mcp_filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Mcp {

Http::FilterFactoryCb McpFilterConfigFactory::createFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::http::mcp::v3::Mcp& proto_config, const std::string&,
    Server::Configuration::FactoryContext&) {

  auto config = std::make_shared<McpFilterConfig>(proto_config);

  return [config](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamFilter(std::make_shared<McpFilter>(config));
  };
}

absl::StatusOr<Router::RouteSpecificFilterConfigConstSharedPtr>
McpFilterConfigFactory::createRouteSpecificFilterConfigTyped(
    const envoy::extensions::filters::http::mcp::v3::McpOverride& proto_config,
    Server::Configuration::ServerFactoryContext&, ProtobufMessage::ValidationVisitor&) {
  return std::make_shared<McpOverrideConfig>(proto_config);
}

/**
 * Static registration for the MCP filter. @see RegisterFactory.
 */
REGISTER_FACTORY(McpFilterConfigFactory, Server::Configuration::NamedHttpFilterConfigFactory);

} // namespace Mcp
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
