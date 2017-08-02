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

namespace Envoy {
namespace Router {

/**
 * The HttpRouteManager exposes the ability to get a RouteConfigProvider. This interface is exposed
 * to the Server's FactoryContext in order to allow HttpConnMans to get RouteConfigProviders.
 */
class HttpRouteManager {
public:
  virtual ~HttpRouteManager() {}

  /**
   * get a Router::RouteConfigProviderSharedPtr. Ownership of the RouteConfigProvider is shared by
   * all the ConnectionManagers who own a Router::RouteConfigProviderSharedPtr. The HttpRouteManager
   * holds weak_ptrs to the RouteConfigProviders. Clean up of the weak ptrs happen from the
   * destructor of the RouteConfigProvider. This function creates a RouteConfigProvider is there
   * isn't one with the same <route_config_name>_<cluster> already. Otherwise, it returns a
   * Router::RouteConfigProviderSharedPtr created from the manager held weak_ptr.
   * @param config supplies the json configuration of an RdsRouteConfigProvider.
   * @param runtime supplies the runtime loader.
   * @param cm supplies the ClusterManager.
   * @param dispatcher supplies the dispatcher.
   * @param random supplies the random generator.
   * @param local_info supplies the local info.
   * @param scope supplies the scope to use for the route config provider.
   * @param stat_prefix supplies the stat_prefix to use for the provider stats.
   * @param tls supplies the tls slot allocator.
   * @param init_manager supplies the init manager.
   */
  virtual RouteConfigProviderSharedPtr
  getRouteConfigProvider(const Json::Object& config, Runtime::Loader& runtime,
                         Upstream::ClusterManager& cm, Event::Dispatcher& dispatcher,
                         Runtime::RandomGenerator& random, const LocalInfo::LocalInfo& local_info,
                         Stats::Scope& scope, const std::string& stat_prefix,
                         ThreadLocal::SlotAllocator& tls, Init::Manager& init_manager) PURE;
};

/**
 * The ServerHttpRouteManager additionally allows listing all of the currently managed
 * RouteConfigProviders.
 */
class ServerHttpRouteManager : public HttpRouteManager {
public:
  virtual ~ServerHttpRouteManager() {}

  /**
   * @return std::vector<Router::RouteConfigProviderSharedPtr> a list of all the
   * RouteConfigProviders currently loaded.
   */
  virtual std::vector<RouteConfigProviderSharedPtr> routeConfigProviders() PURE;
};

} // namespace Router
} // namespace Envoy