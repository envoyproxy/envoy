#pragma once

#include <cstdint>
#include <functional>
#include <string>

#include "envoy/init/init.h"
#include "envoy/json/json_object.h"
#include "envoy/local_info/local_info.h"
#include "envoy/router/rds.h"
#include "envoy/thread_local/thread_local.h"

#include "common/common/logger.h"
#include "common/http/rest_api_fetcher.h"

namespace Envoy {
namespace Router {

/**
 * Route configuration provider utilities.
 */
class RouteConfigProviderUtil {
public:
  /**
   * @return RouteConfigProviderPtr a new route configuration provider based on the supplied JSON
   *         configuration.
   */
  static RouteConfigProviderPtr create(const Json::Object& config, Runtime::Loader& runtime,
                                       Upstream::ClusterManager& cm, Event::Dispatcher& dispatcher,
                                       Runtime::RandomGenerator& random,
                                       const LocalInfo::LocalInfo& local_info, Stats::Scope& scope,
                                       const std::string& stat_prefix,
                                       ThreadLocal::SlotAllocator& tls,
                                       Init::Manager& init_manager);
};

/**
 * Implementation of RouteConfigProvider that holds a static route configuration.
 */
class StaticRouteConfigProviderImpl : public RouteConfigProvider {
public:
  StaticRouteConfigProviderImpl(const Json::Object& config, Runtime::Loader& runtime,
                                Upstream::ClusterManager& cm);

  // Router::RouteConfigProvider
  Router::ConfigConstSharedPtr config() override { return config_; }

private:
  ConfigConstSharedPtr config_;
};

} // namespace Router
} // namespace Envoy
