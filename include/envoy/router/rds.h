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

/**
 * A provider for dynamic route configurations.
 */
class RdsRouteConfigProvider : public RouteConfigProvider {
public:
  virtual ~RdsRouteConfigProvider() {}

  /**
   * @return std::string the loaded route table.
   */
  virtual std::string configAsString() PURE;

  /**
   * @return std::string the name of the configured route table.
   */
  virtual std::string routeConfigName() PURE;

  /**
   * @return std::string the name of the cluster the RdsRouteConfigProvider is issuing RDS requests
   * to.
   */
  virtual std::string clusterName() PURE;
};

typedef std::shared_ptr<RouteConfigProvider> RouteConfigProviderSharedPtr;
typedef std::shared_ptr<RdsRouteConfigProvider> RdsRouteConfigProviderSharedPtr;

} // namespace Router
} // namespace Envoy
