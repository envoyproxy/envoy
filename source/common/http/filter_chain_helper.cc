#include "source/common/http/filter_chain_helper.h"

#include <memory>
#include <string>

#include "envoy/registry/registry.h"

#include "source/common/common/fmt.h"
#include "source/common/config/utility.h"
#include "source/common/http/utility.h"
#include "source/common/protobuf/utility.h"

namespace Envoy {
namespace Http {

void FilterChainUtility::createFilterChainForFactories(
    Http::FilterChainManager& manager, const FilterChainOptions& options,
    const FilterFactoriesList& filter_factories) {
  bool added_missing_config_filter = false;
  for (const auto& filter_config_provider : filter_factories) {
    // If this filter is disabled explicitly, skip trying to create it.
    if (options.filterDisabled(filter_config_provider.provider->name())
            .value_or(filter_config_provider.disabled)) {
      continue;
    }

    auto config = filter_config_provider.provider->config();
    if (config.has_value()) {
      Http::NamedHttpFilterFactoryCb& factory_cb = config.value().get();
      manager.applyFilterFactoryCb({filter_config_provider.provider->name(), factory_cb.name},
                                   factory_cb.factory_cb);
      continue;
    }

    // If a filter config is missing after warming, inject a local reply with status 500.
    if (!added_missing_config_filter) {
      ENVOY_LOG(trace, "Missing filter config for a provider {}",
                filter_config_provider.provider->name());
      manager.applyFilterFactoryCb({}, MissingConfigFilterFactory);
      added_missing_config_filter = true;
    } else {
      ENVOY_LOG(trace, "Provider {} missing a filter config",
                filter_config_provider.provider->name());
    }
  }
}

SINGLETON_MANAGER_REGISTRATION(upstream_filter_config_provider_manager);

std::shared_ptr<UpstreamFilterConfigProviderManager>
FilterChainUtility::createSingletonUpstreamFilterConfigProviderManager(
    Server::Configuration::ServerFactoryContext& context) {
  std::shared_ptr<UpstreamFilterConfigProviderManager> upstream_filter_config_provider_manager =
      context.singletonManager().getTyped<Http::UpstreamFilterConfigProviderManager>(
          SINGLETON_MANAGER_REGISTERED_NAME(upstream_filter_config_provider_manager),
          [] { return std::make_shared<Filter::UpstreamHttpFilterConfigProviderManagerImpl>(); });
  return upstream_filter_config_provider_manager;
}

} // namespace Http
} // namespace Envoy
