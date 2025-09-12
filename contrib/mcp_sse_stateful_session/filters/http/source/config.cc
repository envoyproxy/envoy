#include "contrib/mcp_sse_stateful_session/filters/http/source/config.h"

#include <memory>

#include "envoy/registry/registry.h"

#include "source/server/generic_factory_context.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace McpSseStatefulSession {

Envoy::Http::FilterFactoryCb McpSseStatefulSessionFactoryConfig::createFilterFactoryFromProtoTyped(
    const ProtoConfig& proto_config, const std::string&,
    Server::Configuration::FactoryContext& context) {

  auto filter_config(std::make_shared<McpSseStatefulSessionConfig>(proto_config, context));
  return [filter_config](Envoy::Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamFilter(
        Envoy::Http::StreamFilterSharedPtr{new McpSseStatefulSession(filter_config)});
  };
}

absl::StatusOr<Router::RouteSpecificFilterConfigConstSharedPtr>
McpSseStatefulSessionFactoryConfig::createRouteSpecificFilterConfigTyped(
    const PerRouteProtoConfig& proto_config, Server::Configuration::ServerFactoryContext& context,
    ProtobufMessage::ValidationVisitor& visitor) {
  Server::GenericFactoryContextImpl generic_context(context, visitor);

  return std::make_shared<PerRouteMcpSseStatefulSession>(proto_config, generic_context);
}

REGISTER_FACTORY(McpSseStatefulSessionFactoryConfig,
                 Server::Configuration::NamedHttpFilterConfigFactory);

} // namespace McpSseStatefulSession
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
