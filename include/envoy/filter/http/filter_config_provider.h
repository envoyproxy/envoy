#pragma once

#include "envoy/config/core/v3/config_source.pb.h"
#include "envoy/config/extension_config_provider.h"
#include "envoy/http/filter.h"
#include "envoy/init/manager.h"
#include "envoy/server/filter_config.h"

#include "absl/types/optional.h"

namespace Envoy {
namespace Filter {
namespace Http {

using FilterConfigProvider =
    Envoy::Config::ExtensionConfigProvider<Server::Configuration::NamedHttpFilterConfigFactory,
                                           Envoy::Http::FilterFactoryCb>;
using FilterConfigProviderPtr = std::unique_ptr<FilterConfigProvider>;
using DynamicFilterConfigProvider = Envoy::Config::DynamicExtensionConfigProvider<
    Server::Configuration::NamedHttpFilterConfigFactory, Envoy::Http::FilterFactoryCb>;
using DynamicFilterConfigProviderPtr = std::unique_ptr<DynamicFilterConfigProvider>;

/**
 * The FilterConfigProviderManager exposes the ability to get an FilterConfigProvider
 * for both static and dynamic filter config providers.
 */
class FilterConfigProviderManager {
public:
  virtual ~FilterConfigProviderManager() = default;

  /**
   * Get an FilterConfigProviderPtr for a filter config. The config providers may share
   * the underlying subscriptions to the filter config discovery service.
   * @param config_source supplies the extension configuration source for the filter configs.
   * @param filter_config_name the filter config resource name.
   * @param factory_context is the context to use for the filter config provider.
   * @param stat_prefix supplies the stat_prefix to use for the provider stats.
   */
  virtual DynamicFilterConfigProviderPtr createDynamicFilterConfigProvider(
      const envoy::config::core::v3::ExtensionConfigSource& config_source,
      const std::string& filter_config_name, Server::Configuration::FactoryContext& factory_context,
      const std::string& stat_prefix) PURE;

  /**
   * Get an FilterConfigProviderPtr for a statically inlined filter config.
   * @param config is a fully resolved filter instantiation factory.
   * @param filter_config_name is the name of the filter configuration resource.
   */
  virtual FilterConfigProviderPtr
  createStaticFilterConfigProvider(const Envoy::Http::FilterFactoryCb& config,
                                   const std::string& filter_config_name) PURE;
};

} // namespace Http
} // namespace Filter
} // namespace Envoy
