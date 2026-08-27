#include "source/extensions/filters/http/stateful_session/config.h"

#include <memory>

#include "envoy/registry/registry.h"

#include "source/server/generic_factory_context.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace StatefulSession {

absl::StatusOr<Http::FilterFactoryCb>
StatefulSessionFactoryConfig::createHttpFilterFactoryFromProtoTyped(
    const ProtoConfig& proto_config, Server::Configuration::ServerFactoryContext& context,
    Server::Configuration::ExtraFactoryContext& extra_context) {
  Server::GenericFactoryContextImpl generic_context(
      context, extra_context.scope, extra_context.visitor, extra_context.init_manager);
  auto filter_config(std::make_shared<StatefulSessionConfig>(
      proto_config, generic_context, extra_context.stats_prefix, extra_context.scopeOr(context)));
  return [filter_config](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamFilter(Http::StreamFilterSharedPtr{new StatefulSession(filter_config)});
  };
}

absl::StatusOr<Router::RouteSpecificFilterConfigConstSharedPtr>
StatefulSessionFactoryConfig::createRouteSpecificFilterConfigTyped(
    const PerRouteProtoConfig& proto_config, Server::Configuration::ServerFactoryContext& context,
    ProtobufMessage::ValidationVisitor& visitor) {
  Server::GenericFactoryContextImpl generic_context(context, visitor);

  return std::make_shared<PerRouteStatefulSession>(proto_config, generic_context);
}

REGISTER_FACTORY(StatefulSessionFactoryConfig, Server::Configuration::NamedHttpFilterConfigFactory);

} // namespace StatefulSession
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
