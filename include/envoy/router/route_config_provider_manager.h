#pragma once

#include <string>

#include "envoy/config/filter/network/http_connection_manager/v2/http_connection_manager.pb.h"
#include "envoy/event/dispatcher.h"
#include "envoy/init/init.h"
#include "envoy/json/json_object.h"
#include "envoy/local_info/local_info.h"
#include "envoy/router/rds.h"
#include "envoy/runtime/runtime.h"
#include "envoy/server/filter_config.h"
#include "envoy/stats/stats.h"
#include "envoy/thread_local/thread_local.h"
#include "envoy/upstream/cluster_manager.h"

namespace Envoy {
namespace Router {

/**
 * The RouteConfigProviderManager exposes the ability to get a RouteConfigProvider. This interface
 * is exposed to the Server's FactoryContext in order to allow HttpConnectionManagers to get
 * RouteConfigProviders.
 */
class RouteConfigProviderManager {
public:
  virtual ~RouteConfigProviderManager() {}

  /**
   * Get a RouteConfigProviderPtr for a route from RDS. Ownership of the RouteConfigProvider is the
   * HttpConnectionManagers who calls this function. The RouteConfigProviderManager holds raw
   * pointers to the RouteConfigProviders. Clean up of the pointers happen from the destructor of
   * the RouteConfigProvider. This method creates a RouteConfigProvider which may share the
   * underlying RDS subscription with the same (route_config_name, cluster).
   * @param rds supplies the proto configuration of an RDS-configured RouteConfigProvider.
   * @param factory_context is the context to use for the route config provider.
   * @param stat_prefix supplies the stat_prefix to use for the provider stats.
   */
  virtual RouteConfigProviderPtr createRdsRouteConfigProvider(
      const envoy::config::filter::network::http_connection_manager::v2::Rds& rds,
      Server::Configuration::FactoryContext& factory_context, const std::string& stat_prefix) PURE;

  /**
   * Get a RouteConfigSharedPtr for a statically defined route. Ownership is as described for
   * getRdsRouteConfigProvider above. This method always create a new RouteConfigProvider.
   * @param route_config supplies the RouteConfiguration for this route
   * @param runtime supplies the runtime loader.
   * @param cm supplies the ClusterManager.
   */
  virtual RouteConfigProviderPtr
  createStaticRouteConfigProvider(const envoy::api::v2::RouteConfiguration& route_config,
                                  Server::Configuration::FactoryContext& factory_context) PURE;
};

} // namespace Router
} // namespace Envoy
