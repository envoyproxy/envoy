#include "source/extensions/filters/http/stateful_session/config.h"

#include <memory>

#include "envoy/registry/registry.h"

#include "source/server/generic_factory_context.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace StatefulSession {

Http::FilterFactoryCb StatefulSessionFactoryConfig::createFilterFactoryFromProtoTyped(
    const ProtoConfig& proto_config, const std::string& stats_prefix,
    Server::Configuration::FactoryContext& context) {
  auto filter_config(std::make_shared<StatefulSessionConfig>(proto_config, context));
  const std::string final_prefix =
      absl::StrCat(stats_prefix, "stateful_session.", filter_config->statPrefixOverride());
  auto stats = std::make_shared<StatefulSessionFilterStats>(StatefulSessionFilterStats{
      ALL_STATEFUL_SESSION_FILTER_STATS(POOL_COUNTER_PREFIX(context.scope(), final_prefix))});
  filter_config->setStats(stats);
  return [filter_config, stats](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamFilter(
        Http::StreamFilterSharedPtr{new StatefulSession(filter_config, stats)});
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
