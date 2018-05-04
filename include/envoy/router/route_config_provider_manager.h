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
   * Get a RouteConfigProviderSharedPtr for a route from RDS. Ownership of the RouteConfigProvider
   * is shared by all the HttpConnectionManagers who own a RouteConfigProviderSharedPtr. The
   * RouteConfigProviderManager holds weak_ptrs to the RouteConfigProviders. Clean up of the weak
   * ptrs happen from the destructor of the RouteConfigProvider. This function creates a
   * RouteConfigProvider if there isn't one with the same (route_config_name, cluster) already.
   * Otherwise, it returns a RouteConfigProviderSharedPtr created from the manager held weak_ptr.
   * @param rds supplies the proto configuration of an RDS-configured RouteConfigProvider.
   * @param cm supplies the ClusterManager.
   * @param scope supplies the scope to use for the route config provider.
   * @param stat_prefix supplies the stat_prefix to use for the provider stats.
   * @param init_manager supplies the init manager.
   */
  virtual RouteConfigProviderSharedPtr getRdsRouteConfigProvider(
      const envoy::config::filter::network::http_connection_manager::v2::Rds& rds,
      Server::Configuration::FactoryContext& factory_context, const std::string& stat_prefix) PURE;

  /**
   * Get a RouteConfigSharedPtr for a statically defined route. Ownership is as described for
   * getRdsRouteConfigProvider above. Unlike getRdsRouteConfigProvider(), this method always creates
   * a new RouteConfigProvider.
   * @param route_config supplies the RouteConfiguration for this route
   * @param runtime supplies the runtime loader.
   * @param cm supplies the ClusterManager.
   */
  virtual RouteConfigProviderSharedPtr
  getStaticRouteConfigProvider(const envoy::api::v2::RouteConfiguration& route_config,
                               Server::Configuration::FactoryContext& factory_context) PURE;

  /**
   * @return std::vector<Router::RouteConfigProviderSharedPtr> a list of all the
   * dynamic (RDS) RouteConfigProviders currently loaded.
   */
  virtual std::vector<RouteConfigProviderSharedPtr> getRdsRouteConfigProviders() PURE;

  /**
   * @return std::vector<Router::RouteConfigProviderSharedPtr> a list of all the
   * static RouteConfigProviders currently loaded.
   */
  virtual std::vector<RouteConfigProviderSharedPtr> getStaticRouteConfigProviders() PURE;
};

} // namespace Router
} // namespace Envoy
