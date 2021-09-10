#pragma once

#include <memory>

#include "envoy/config/route/v3/route.pb.h"
#include "envoy/http/filter.h"
#include "envoy/router/rds/route_config_provider.h"
#include "envoy/router/router.h"

namespace Envoy {
namespace Router {

class RouteConfigProvider
    : public Rds::RouteConfigProvider<envoy::config::route::v3::RouteConfiguration, Config> {
public:
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
