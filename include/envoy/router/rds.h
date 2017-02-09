#pragma once

#include "envoy/router/router.h"

namespace Router {

/**
 * A provider for constant route configurations.
 */
class RouteConfigProvider {
public:
  virtual ~RouteConfigProvider() {}

  /**
   * @return Router::ConfigPtr a route configuration for use during a single request. The returned
   *         config may be different on a subsequent call so a new config should be acquired for
   *         each request flow.
   */
  virtual Router::ConfigPtr config() PURE;
};

typedef std::unique_ptr<RouteConfigProvider> RouteConfigProviderPtr;

} // Router
