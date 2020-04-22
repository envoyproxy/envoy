#pragma once

#include <memory>

#include "envoy/config/route/v3/route.pb.h"
#include "envoy/http/filter.h"
#include "envoy/router/router.h"

namespace Envoy {
namespace Router {

/**
 * A provider for constant route configurations.
 */
class RouteConfigProvider {
public:
  struct ConfigInfo {
    // A reference to the currently loaded route configuration. Do not hold this reference beyond
    // the caller of configInfo()'s scope.
    const envoy::config::route::v3::RouteConfiguration& config_;

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
  virtual Router::ConfigConstSharedPtr config() PURE;

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
  virtual void
  validateConfig(const envoy::config::route::v3::RouteConfiguration& config) const PURE;

  /**
   * Callback used to request an update to the route configuration from the management server.
   * @param for_domain supplies the domain name that virtual hosts must match on
   * @param thread_local_dispatcher thread-local dispatcher
   * @param route_config_updated_cb callback to be called when the configuration update has been
   * propagated to worker threads
   */
  virtual void requestVirtualHostsUpdate(
      const std::string& for_domain, Event::Dispatcher& thread_local_dispatcher,
      std::weak_ptr<Http::RouteConfigUpdatedCallback> route_config_updated_cb) PURE;
};

using RouteConfigProviderPtr = std::unique_ptr<RouteConfigProvider>;
using RouteConfigProviderSharedPtr = std::shared_ptr<RouteConfigProvider>;

} // namespace Router
} // namespace Envoy
