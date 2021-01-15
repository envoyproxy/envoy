#pragma once

#include "envoy/config/core/v3/config_source.pb.h"
#include "envoy/config/extension_config_provider.h"
#include "envoy/init/manager.h"
#include "envoy/server/filter_config.h"

#include "absl/types/optional.h"

namespace Envoy {
namespace Filter {

template <class FactoryCallback>
using FilterConfigProvider = Envoy::Config::ExtensionConfigProvider<FactoryCallback>;
template <class FactoryCallback>
using FilterConfigProviderPtr = std::unique_ptr<FilterConfigProvider<FactoryCallback>>;

/**
 * The FilterConfigProviderManager exposes the ability to get a FilterConfigProvider
 * for both static and dynamic filter config providers.
 */
template <class FactoryCallback> class FilterConfigProviderManager {
public:
  virtual ~FilterConfigProviderManager() = default;

  /**
   * Get an FilterConfigProviderPtr for a filter config. The config providers may share
   * the underlying subscriptions to the filter config discovery service.
   * @param config_source supplies the extension configuration source for the filter configs.
   * @param filter_config_name the filter config resource name.
   * @param factory_context is the context to use for the filter config provider.
   * @param stat_prefix supplies the stat_prefix to use for the provider stats.
   * @param is_terminal indicates whether the filter is the last in its filter chain.
   */
  virtual FilterConfigProviderPtr<FactoryCallback> createDynamicFilterConfigProvider(
      const envoy::config::core::v3::ExtensionConfigSource& config_source,
      const std::string& filter_config_name, Server::Configuration::FactoryContext& factory_context,
      const std::string& stat_prefix, bool is_terminal) PURE;

  /**
   * Get an FilterConfigProviderPtr for a statically inlined filter config.
   * @param config is a fully resolved filter instantiation factory.
   * @param filter_config_name is the name of the filter configuration resource.
   */
  virtual FilterConfigProviderPtr<FactoryCallback>
  createStaticFilterConfigProvider(const FactoryCallback& config,
                                   const std::string& filter_config_name) PURE;
};

} // namespace Filter
} // namespace Envoy
