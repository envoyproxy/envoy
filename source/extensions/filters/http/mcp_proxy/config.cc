#include "source/extensions/filters/http/mcp_proxy/config.h"
#include "source/extensions/filters/http/mcp_proxy/mcp_filter.h"
namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace McpProxy {
Http::FilterFactoryCb McpProxyFilterConfig::createFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::http::mcp_proxy::v3::McpProxy& proto_config,
    const std::string&, Server::Configuration::FactoryContext& context) {
  McpFilterConfigSharedPtr filter_config = std::make_shared<McpFilterConfig>(proto_config, context);
  return [filter_config](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamFilter(Http::StreamFilterSharedPtr{new McpFilter(filter_config)});
  };
}
Router::RouteSpecificFilterConfigConstSharedPtr
McpProxyFilterConfig::createRouteSpecificFilterConfigTyped(
    const envoy::extensions::filters::http::mcp_proxy::v3::McpProxy& proto_config,
    Server::Configuration::ServerFactoryContext& context, ProtobufMessage::ValidationVisitor&) {
  return std::make_shared<McpFilterConfig>(proto_config, context);
}
REGISTER_FACTORY(McpProxyFilterConfig, Server::Configuration::NamedHttpFilterConfigFactory);
} // namespace McpProxy
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
