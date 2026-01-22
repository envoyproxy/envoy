#include "source/extensions/filters/http/filter_chain/config.h"

#include "envoy/registry/registry.h"

#include "source/extensions/filters/http/filter_chain/filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace FilterChain {

namespace {

OptRef<const FilterChain> routeLevelFilterChain(FilterChainConfig& filter_chain_config,
                                                Http::FilterChainFactoryCallbacks& callbacks) {
  const OptRef<const Router::Route> route = callbacks.route();
  if (!route.has_value()) {
    filter_chain_config.stats().no_route_.inc();
    return {};
  }
  const auto* per_route_config = dynamic_cast<const FilterChainPerRouteConfig*>(
      route->mostSpecificPerFilterConfig(callbacks.filterConfigName()));
  if (per_route_config == nullptr) {
    filter_chain_config.stats().no_route_filter_config_.inc();
    return {};
  }
  if (auto chain = per_route_config->filterChain(); chain.has_value()) {
    filter_chain_config.stats().use_route_filter_chain_.inc();
    return chain;
  }
  if (auto chain = filter_chain_config.filterChain(per_route_config->filterChainName());
      chain.has_value()) {
    filter_chain_config.stats().use_named_filter_chain_.inc();
    return chain;
  }
  filter_chain_config.stats().no_matched_filter_chain_.inc();
  return {};
}

} // namespace

Http::FilterFactoryCb FilterChainFilterFactory::createFilterFactoryFromProtoTyped(
    const FilterChainConfigProto& proto_config, const std::string& stats_prefix,
    Server::Configuration::FactoryContext& context) {
  auto filter_config = std::make_shared<FilterChainConfig>(proto_config, context, stats_prefix);

  return [filter_config](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    OptRef<const FilterChain> chain = routeLevelFilterChain(*filter_config, callbacks);

    // If no route level filter chain, use the default one.
    if (!chain.has_value()) {
      if (chain = filter_config->filterChain(); !chain.has_value()) {
        ENVOY_LOG(debug, "filter chain filter: no filter chain found, passing through");
        filter_config->stats().pass_through_.inc();
        return;
      }
      filter_config->stats().use_default_filter_chain_.inc();
    }

    ASSERT(chain.has_value());
    Http::FilterChainUtility::createFilterChainForFactories(callbacks, chain->filterFactories());
  };
}

absl::StatusOr<Router::RouteSpecificFilterConfigConstSharedPtr>
FilterChainFilterFactory::createRouteSpecificFilterConfigTyped(
    const envoy::extensions::filters::http::filter_chain::v3::FilterChainConfigPerRoute&
        proto_config,
    Server::Configuration::ServerFactoryContext& context, ProtobufMessage::ValidationVisitor&) {
  absl::Status creation_status = absl::OkStatus();
  // TODO(wbpcode): use the route name or vhost name as stats prefix?
  auto filter_config = std::make_shared<FilterChainPerRouteConfig>(
      proto_config, context, "filter_chain.", creation_status);
  RETURN_IF_NOT_OK(creation_status);
  return filter_config;
}

/**
 * Static registration for the filter chain filter. @see RegisterFactory.
 */
REGISTER_FACTORY(FilterChainFilterFactory, Server::Configuration::NamedHttpFilterConfigFactory);

} // namespace FilterChain
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
