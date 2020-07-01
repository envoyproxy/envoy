#pragma once

#include "envoy/config/core/v3/config_source.pb.h"
#include "envoy/config/core/v3/extension.pb.h"
#include "envoy/http/filter.h"
#include "envoy/init/manager.h"
#include "envoy/server/filter_config.h"

namespace Envoy {
namespace Router {

/**
 * A provider for constant filter configurations.
 */
class FilterConfigProvider {
public:
  virtual ~FilterConfigProvider() = default;

  /**
   * Get the filter config resource name.
   **/
  virtual const std::string& name() PURE;

  /**
   * @return Http::FilterFactoryCb a filter config to be instantiated on the subsequent streams.
   * Note that if the provider has not yet performed an initial configuration load and no default is
   * provided, no information will be returned.
   */
  virtual absl::optional<Http::FilterFactoryCb> config() PURE;

  /**
   * Validate if the route configuration can be applied in the context of the filter manager.
   * @param Server::Configuration::NamedHttpFilterConfigFactory a filter factory to validate in the
   * context of the filter manager filter chains.
   */
  virtual void validateConfig(Server::Configuration::NamedHttpFilterConfigFactory& factory) PURE;

  /**
   * Update the provider about the configuration changes.
   * @param config is a filter factory to be used on the subsequent streams.
   * @param version_info is the version of the new filter configuration.
   */
  virtual void onConfigUpdate(Http::FilterFactoryCb config, const std::string& version_info) PURE;
};

using FilterConfigProviderPtr = std::unique_ptr<FilterConfigProvider>;

/**
 * The FilterConfigProviderManager exposes the ability to get a FilterConfigProvider. This interface
 * is exposed to the Server's FactoryContext in order to allow stream creators to get
 * FilterConfigProviders for filters on the stream.
 */
class FilterConfigProviderManager {
public:
  virtual ~FilterConfigProviderManager() = default;

  /**
   * Get a FilterConfigProviderPtr for a filter config. The config providers may share
   * the underlying subscriptions to the filter config discovery service.
   * @param config_source supplies the configuration source for the filter configs.
   * @param filter_config_name the filter config resource name.
   * @param require_terminal enforces that the filter config must be for a terminal filter
   * @param factory_context is the context to use for the filter config provider.
   * @param stat_prefix supplies the stat_prefix to use for the provider stats.
   * @param apply_without_warming initializes immediately with the default config and starts the
   * subscription.
   */
  virtual FilterConfigProviderPtr
  createDynamicFilterConfigProvider(const envoy::config::core::v3::ConfigSource& config_source,
                                    const std::string& filter_config_name, bool require_terminal,
                                    Server::Configuration::FactoryContext& factory_context,
                                    const std::string& stat_prefix,
                                    bool apply_without_warming) PURE;

  /**
   * Get a FilterConfigProviderPtr for a statically inlined filter config.
   * @param config is a fully resolved filter instantiation factory.
   * @param filter_config_name the filter config resource name.
   */
  virtual FilterConfigProviderPtr
  createStaticFilterConfigProvider(const Http::FilterFactoryCb& config,
                                   const std::string& filter_config_name) PURE;
};

} // namespace Router
} // namespace Envoy
