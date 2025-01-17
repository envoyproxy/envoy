#pragma once

#include <memory>

#include "envoy/init/manager.h"
#include "envoy/rds/route_config_provider.h"
#include "envoy/server/factory_context.h"

namespace Envoy {
namespace Rds {
namespace Common {

/**
 * The RouteConfigProviderManager exposes the ability to get a RouteConfigProvider. This interface
 * is exposed to the Server's FactoryContext in order to allow the protocol to get
 * RouteConfigProviders.
 * This basic interface can be used in simple protocols where no other parameters are required.
 */
template <class Rds, class RouteConfiguration> class RouteConfigProviderManager {
public:
  virtual ~RouteConfigProviderManager() = default;

  /**
   * Get a RouteConfigProviderPtr for a route from RDS. Ownership of the RouteConfigProvider is the
   * caller of this function. The RouteConfigProviderManager holds raw
   * pointers to the RouteConfigProviders. Clean up of the pointers happen from the destructor of
   * the RouteConfigProvider. This method creates a RouteConfigProvider which may share the
   * underlying RDS subscription with the same (route_config_name, cluster).
   * @param rds supplies the proto configuration of an RDS-configured RouteConfigProvider.
   * @param factory_context is the context to use for the route config provider.
   * @param stat_prefix supplies the stat_prefix to use for the provider stats.
   * @param init_manager the Init::Manager used to coordinate initialization of a the underlying RDS
   * subscription.
   */
  virtual RouteConfigProviderSharedPtr
  createRdsRouteConfigProvider(const Rds& rds,
                               Server::Configuration::ServerFactoryContext& factory_context,
                               const std::string& stat_prefix, Init::Manager& init_manager) PURE;

  /**
   * Get a RouteConfigSharedPtr for a statically defined route. Ownership is as described for
   * createRdsRouteConfigProvider above. This method always creates a new RouteConfigProvider.
   * @param route_config supplies the RouteConfiguration for this route
   * @param factory_context is the context to use for the route config provider.
   */
  virtual RouteConfigProviderPtr createStaticRouteConfigProvider(
      const RouteConfiguration& route_config,
      Server::Configuration::ServerFactoryContext& factory_context) PURE;
};

} // namespace Common
} // namespace Rds
} // namespace Envoy
