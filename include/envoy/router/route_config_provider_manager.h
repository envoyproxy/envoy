#pragma once

#include <string>

#include "envoy/event/dispatcher.h"
#include "envoy/init/init.h"
#include "envoy/json/json_object.h"
#include "envoy/local_info/local_info.h"
#include "envoy/router/rds.h"
#include "envoy/runtime/runtime.h"
#include "envoy/stats/stats.h"
#include "envoy/thread_local/thread_local.h"
#include "envoy/upstream/cluster_manager.h"

#include "api/filter/network/http_connection_manager.pb.h"

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
   * Get a RouteConfigProviderSharedPtr. Ownership of the RouteConfigProvider is shared by
   * all the HttpConnectionManagers who own a RouteConfigProviderSharedPtr. The
   * RouteConfigProviderManager holds weak_ptrs to the RouteConfigProviders. Clean up of the weak
   * ptrs happen from the destructor of the RouteConfigProvider. This function creates a
   * RouteConfigProvider if there isn't one with the same (route_config_name, cluster) already.
   * Otherwise, it returns a RouteConfigProviderSharedPtr created from the manager held weak_ptr.
   * @param rds supplies the proto configuration of an RdsRouteConfigProvider.
   * @param scope supplies the scope to use for the route config provider.
   * @param stat_prefix supplies the stat_prefix to use for the provider stats.
   * @param init_manager supplies the init manager.
   */
  virtual RouteConfigProviderSharedPtr
  getRouteConfigProvider(const envoy::api::v2::filter::network::Rds& rds,
                         Upstream::ClusterManager& cm, Stats::Scope& scope,
                         const std::string& stat_prefix, Init::Manager& init_manager) PURE;
};

/**
 * The ServerRouteConfigProviderManager additionally allows listing all of the currently managed
 * RouteConfigProviders.
 */
class ServerRouteConfigProviderManager : public RouteConfigProviderManager {
public:
  virtual ~ServerRouteConfigProviderManager() {}

  /**
   * @return std::vector<Router::RdsRouteConfigProviderSharedPtr> a list of all the
   * RdsRouteConfigProviders currently loaded. This means that the manager does not provide
   * pointers to StaticRouteConfigProviders.
   */
  virtual std::vector<RdsRouteConfigProviderSharedPtr> rdsRouteConfigProviders() PURE;
};

} // namespace Router
} // namespace Envoy
