#include "source/extensions/filters/http/filter_chain/config.h"

#include "envoy/registry/registry.h"

#include "source/extensions/filters/http/filter_chain/filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace FilterChain {

Http::FilterFactoryCb FilterChainFilterFactory::createFilterFactoryFromProtoTyped(
    const FilterChainConfigProto& proto_config, const std::string& stats_prefix,
    Server::Configuration::FactoryContext& context) {
  auto filter_config = std::make_shared<FilterChainConfig>(proto_config, context, stats_prefix);

  return [filter_config](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    OptRef<const FilterChain> chain;
    absl::string_view chain_name;

    // Check for per-route filter chain configuration first.
    if (const auto initial_route = callbacks.route(); initial_route.has_value()) {
      const auto* per_route_config = dynamic_cast<const FilterChainPerRouteConfig*>(
          initial_route->mostSpecificPerFilterConfig(callbacks.filterConfigName()));
      if (per_route_config != nullptr) {
        chain = per_route_config->filterChain();
        if (!chain.has_value()) {
          chain_name = per_route_config->filterChainName();
        }
      }
    }

    // If no per-route level filter, then try the per-route name.
    if (!chain.has_value()) {
      // No per-route filter chain configured, use default or named filter chain.
      chain = filter_config->filterChain(chain_name);
    }

    if (!chain.has_value() || chain->filterFactories().empty()) {
      ENVOY_LOG(debug, "filter chain filter: no filter chain found, passing through");
      return;
    }

    Http::FilterChainUtility::createFilterChainForFactories(callbacks, chain->filterFactories());
  };
}

absl::StatusOr<Router::RouteSpecificFilterConfigConstSharedPtr>
FilterChainFilterFactory::createRouteSpecificFilterConfigTyped(
    const envoy::extensions::filters::http::filter_chain::v3::FilterChainConfigPerRoute&
        proto_config,
    Server::Configuration::ServerFactoryContext& context, ProtobufMessage::ValidationVisitor&) {
  // TODO(wbpcode): use the route name or vhost name as stats prefix?
  return std::make_shared<FilterChainPerRouteConfig>(proto_config, context, "filter_chain.");
}

/**
 * Static registration for the filter chain filter. @see RegisterFactory.
 */
REGISTER_FACTORY(FilterChainFilterFactory, Server::Configuration::NamedHttpFilterConfigFactory);

} // namespace FilterChain
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
