#pragma once

#include <memory>

#include "envoy/router/router.h"

namespace Envoy {
namespace Router {

/**
 * A provider for constant route configurations.
 */
class RouteConfigProvider {
public:
  virtual ~RouteConfigProvider() {}

  /**
   * @return Router::ConfigConstSharedPtr a route configuration for use during a single request. The
   * returned
   *         config may be different on a subsequent call, so a new config should be acquired for
   *         each request flow.
   */
  virtual Router::ConfigConstSharedPtr config() PURE;
};

typedef std::unique_ptr<RouteConfigProvider> RouteConfigProviderPtr;

} // namespace Router
} // namespace Envoy
