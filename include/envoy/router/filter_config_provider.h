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
   * @return Http::FilterFctoryCb a filter config to be instantiated on the subsequent streams. Note
   * that if the provider has not yet performed an initial configuration load and no default is
   * provided, no information will be returned.
   */
  virtual absl::optional<Http::FilterFactoryCb> config() PURE;

  /**
   * Validate if the route configuration can be applied to the context of the filter manager.
   * @param Server::Configuration::NamedHttpFilterConfigFactory a filter factory to validate in the
   * context of the filter manager filter chains.
   */
  virtual void validateConfig(Server::Configuration::NamedHttpFilterConfigFactory& factory) PURE;

  /**
   * Callback used to notify provider about configuration changes.
   */
  // virtual void onConfigUpdate() PURE;
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
   * @param name the filter config resource name
   * @param factory_context is the context to use for the filter config provider.
   * @param stat_prefix supplies the stat_prefix to use for the provider stats.
   * @param init_manager the Init::Manager used to coordinate initialization of a the underlying
   * subscription.
   */
  virtual FilterConfigProviderPtr createDynamicFilterConfigProvider(
      const envoy::config::core::v3::ConfigSource& config_source, const std::string& name,
      Server::Configuration::ServerFactoryContext& factory_context, const std::string& stat_prefix,
      Init::Manager& init_manager) PURE;

  /**
   * Get a FilterConfigProviderPtr for a statically inlined filter config.
   * @param callback is a fully resolved filter instantiation callback
   */
  virtual FilterConfigProviderPtr
  createStaticFilterConfigProvider(const Http::FilterFactoryCb& callback) PURE;
};

} // namespace Router
} // namespace Envoy
