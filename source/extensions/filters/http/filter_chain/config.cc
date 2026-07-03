#include "source/extensions/filters/http/filter_chain/config.h"

#include "envoy/registry/registry.h"

#include "source/extensions/filters/http/filter_chain/filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace FilterChain {

namespace {

using FilterChains = absl::InlinedVector<const FilterChain*, 5>;
// HCM -> RouteConfiguration -> VirtualHost -> Route -> WeightedCluster
FilterChains getAllFilterChains(OptRef<const FilterChain> default_chain,
                                Http::FilterChainFactoryCallbacks& callbacks) {
  FilterChains filter_chains;
  if (default_chain.has_value()) {
    filter_chains.push_back(default_chain.ptr());
  }

  const OptRef<const Router::Route> route = callbacks.route();
  if (!route.has_value()) {
    return filter_chains;
  }

  for (auto config : route->perFilterConfigs(callbacks.filterConfigName())) {
    auto* route_chain = dynamic_cast<const FilterChainPerRouteConfig*>(config);
    if (route_chain == nullptr) {
      continue;
    }
    if (auto chain = route_chain->filterChain(); chain.has_value()) {
      filter_chains.push_back(chain.ptr());
    }
  }
  return filter_chains;
}

bool hasFilter(absl::string_view filter_name, absl::Span<const FilterChain*> filter_chains) {
  for (const auto* filter_chain : filter_chains) {
    ASSERT(filter_chain != nullptr);
    if (filter_chain->hasFilter(filter_name)) {
      return true;
    }
  }
  return false;
}

void createFilterChain(Http::FilterChainFactoryCallbacks& callbacks,
                       const FilterChain* filter_chain,
                       absl::Span<const FilterChain*> more_specific_filter_chains) {
  ASSERT(filter_chain != nullptr);

  for (const auto& config_provider : filter_chain->filterFactories()) {
    absl::string_view filter_config_name = config_provider->name();

    // If there is a more specific filter chain that has the same name filter, skip this one.
    if (hasFilter(filter_config_name, more_specific_filter_chains)) {
      continue;
    }

    auto config = config_provider->config();
    // In the filter_chain filter, we only has static filter config providers, so the config should
    // always be available.
    if (config.has_value()) {
      callbacks.setFilterConfigName(filter_config_name);
      config.value()(callbacks);
    }
  }
}

} // namespace

Http::FilterFactoryCb FilterChainFilterFactory::createFilterFactoryFromProtoTyped(
    const FilterChainConfigProto& proto_config, const std::string& stats_prefix,
    Server::Configuration::FactoryContext& context) {
  auto filter_config = std::make_shared<FilterChainConfig>(proto_config, context, stats_prefix);

  return [filter_config](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    FilterChains filter_chains = getAllFilterChains(filter_config->filterChain(), callbacks);
    if (filter_chains.empty()) {
      filter_config->stats().pass_through_.inc();
      return;
    }
    absl::Span<const FilterChain*> chains_span = absl::MakeSpan(filter_chains);

    for (size_t i = 0; i < chains_span.size(); ++i) {
      ASSERT(chains_span[i] != nullptr);
      createFilterChain(callbacks, chains_span[i], chains_span.subspan(i + 1));
    }
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
