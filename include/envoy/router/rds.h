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
   * @return std::string the loaded route table in JSON format.
   */
  virtual std::string configAsJson() const PURE;

  /**
   * @return const std::string& the name of the configured route table.
   */
  virtual const std::string& routeConfigName() const PURE;

  /**
   * @return const std::string& the name of the cluster the RdsRouteConfigProvider is issuing RDS
   * requests to.
   */
  virtual const std::string& clusterName() const PURE;
};

typedef std::shared_ptr<RouteConfigProvider> RouteConfigProviderSharedPtr;
typedef std::shared_ptr<RdsRouteConfigProvider> RdsRouteConfigProviderSharedPtr;

} // namespace Router
} // namespace Envoy
