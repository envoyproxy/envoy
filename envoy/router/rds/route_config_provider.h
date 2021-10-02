#pragma once

#include <memory>

#include "envoy/common/time.h"

#include "absl/types/optional.h"

namespace Envoy {
namespace Router {
namespace Rds {

/**
 * A provider for constant route configurations.
 */
template <class RouteConfiguration, class Config> class RouteConfigProvider {
public:
  struct ConfigInfo {
    // A reference to the currently loaded route configuration. Do not hold this reference beyond
    // the caller of configInfo()'s scope.
    const RouteConfiguration& config_;

    // The discovery version that supplied this route. This will be set to "" in the case of
    // static clusters.
    std::string version_;
  };

  virtual ~RouteConfigProvider() = default;

  /**
   * @return Router::ConfigConstSharedPtr a route configuration for use during a single request. The
   * returned config may be different on a subsequent call, so a new config should be acquired for
   * each request flow.
   */
  virtual std::shared_ptr<const Config> config() PURE;

  /**
   * @return the configuration information for the currently loaded route configuration. Note that
   * if the provider has not yet performed an initial configuration load, no information will be
   * returned.
   */
  virtual absl::optional<ConfigInfo> configInfo() const PURE;

  /**
   * @return the last time this RouteConfigProvider was updated. Used for config dumps.
   */
  virtual SystemTime lastUpdated() const PURE;

  /**
   * Callback used to notify RouteConfigProvider about configuration changes.
   */
  virtual void onConfigUpdate() PURE;

  /**
   * Validate if the route configuration can be applied to the context of the route config provider.
   */
  virtual void validateConfig(const RouteConfiguration& config) const PURE;
};

template <class RouteConfiguration, class Config>
using RouteConfigProviderPtr = std::unique_ptr<RouteConfigProvider<RouteConfiguration, Config>>;
template <class RouteConfiguration, class Config>
using RouteConfigProviderSharedPtr =
    std::shared_ptr<RouteConfigProvider<RouteConfiguration, Config>>;

} // namespace Rds
} // namespace Router
} // namespace Envoy
