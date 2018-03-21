#pragma once

#include <memory>

#include "envoy/api/v2/rds.pb.h"
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
   * @return envoy::api::v2::RouteConfiguration the underlying RouteConfiguration object associated
   * with this provider.
   */
  virtual const envoy::api::v2::RouteConfiguration& configAsProto() const PURE;

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

typedef std::shared_ptr<RouteConfigProvider> RouteConfigProviderSharedPtr;

} // namespace Router
} // namespace Envoy
