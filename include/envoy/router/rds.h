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

  /**
   * @return const std::string version info from last accepted config.
   *
   * TODO(dnoe): This would ideally return by reference, but this causes a
   *             problem due to incompatible string implementations returned by
   *             protobuf generated code. Revisit when string implementations
   *             are converged.
   */
  virtual const std::string versionInfo() const PURE;
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
   * @return const std::string& the configuration of the service the RdsRouteConfigProvider is
   * issuing RDS requests to.
   */
  virtual const std::string& configSource() const PURE;
};

typedef std::shared_ptr<RouteConfigProvider> RouteConfigProviderSharedPtr;
typedef std::shared_ptr<RdsRouteConfigProvider> RdsRouteConfigProviderSharedPtr;

} // namespace Router
} // namespace Envoy
