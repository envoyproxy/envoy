#pragma once

#include "envoy/config/core/v3/config_source.pb.h"
#include "envoy/config/extension_config_provider.h"
#include "envoy/http/filter.h"
#include "envoy/init/manager.h"
#include "envoy/server/filter_config.h"

#include "absl/types/optional.h"

namespace Envoy {
namespace Filter {

using HttpFilterConfigProvider =
    Envoy::Config::ExtensionConfigProvider<Server::Configuration::NamedHttpFilterConfigFactory,
                                           Http::FilterFactoryCb>;
using HttpFilterConfigProviderPtr = std::unique_ptr<HttpFilterConfigProvider>;

/**
 * The HttpFilterConfigProviderManager exposes the ability to get an HttpFilterConfigProvider
 * for both static and dynamic filter config providers.
 */
class HttpFilterConfigProviderManager {
public:
  virtual ~HttpFilterConfigProviderManager() = default;

  /**
   * Get an HttpFilterConfigProviderPtr for a filter config. The config providers may share
   * the underlying subscriptions to the filter config discovery service.
   * @param config_source supplies the configuration source for the filter configs.
   * @param filter_config_name the filter config resource name.
   * @param require_terminal enforces that the filter config must be for a terminal filter.
   * @param require_type_url enforces that the typed filter config must have a certain type URL.
   * @param factory_context is the context to use for the filter config provider.
   * @param stat_prefix supplies the stat_prefix to use for the provider stats.
   * @param apply_without_warming initializes immediately with the default config and starts the
   * subscription.
   */
  virtual HttpFilterConfigProviderPtr
  createDynamicFilterConfigProvider(const envoy::config::core::v3::ConfigSource& config_source,
                                    const std::string& filter_config_name, bool require_terminal,
                                    absl::optional<std::string> require_type_url,
                                    Server::Configuration::FactoryContext& factory_context,
                                    const std::string& stat_prefix,
                                    bool apply_without_warming) PURE;

  /**
   * Get an HttpFilterConfigProviderPtr for a statically inlined filter config.
   * @param config is a fully resolved filter instantiation factory.
   * @param filter_config_name is the name of the filter configuration resource.
   */
  virtual HttpFilterConfigProviderPtr
  createStaticFilterConfigProvider(const Http::FilterFactoryCb& config,
                                   const std::string& filter_config_name) PURE;
};

} // namespace Filter
} // namespace Envoy
