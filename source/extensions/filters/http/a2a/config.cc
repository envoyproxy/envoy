#include "source/extensions/filters/http/a2a/config.h"

#include "envoy/registry/registry.h"

#include "source/extensions/filters/http/a2a/a2a_filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace A2a {

Http::FilterFactoryCb A2aFilterConfigFactory::createFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::http::a2a::v3::A2a& proto_config,
    const std::string& stats_prefix, Server::Configuration::FactoryContext& context) {

  // Use the server_factory_context to access the root scope for stats
  auto config = std::make_shared<A2aFilterConfig>(proto_config, stats_prefix,
                                                  context.serverFactoryContext().scope());

  return [config](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamFilter(std::make_shared<A2aFilter>(config));
  };
}

absl::StatusOr<Router::RouteSpecificFilterConfigConstSharedPtr>
A2aFilterConfigFactory::createRouteSpecificFilterConfigTyped(
    const envoy::extensions::filters::http::a2a::v3::A2aOverride& proto_config,
    Server::Configuration::ServerFactoryContext&, ProtobufMessage::ValidationVisitor&) {
  return std::make_shared<const A2aOverrideConfig>(proto_config);
}

/**
 * Static registration for the A2A filter. @see RegisterFactory.
 */
REGISTER_FACTORY(A2aFilterConfigFactory, Server::Configuration::NamedHttpFilterConfigFactory);

} // namespace A2a
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
