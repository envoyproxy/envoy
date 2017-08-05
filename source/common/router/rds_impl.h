#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>

#include "envoy/init/init.h"
#include "envoy/json/json_object.h"
#include "envoy/local_info/local_info.h"
#include "envoy/router/rds.h"
#include "envoy/router/route_config_provider_manager.h"
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
  static RouteConfigProviderSharedPtr
  create(const Json::Object& config, Runtime::Loader& runtime, Upstream::ClusterManager& cm,
         Stats::Scope& scope, const std::string& stat_prefix, Init::Manager& init_manager,
         RouteConfigProviderManager& route_config_provider_manager);
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

/**
 * All RDS stats. @see stats_macros.h
 */
// clang-format off
#define ALL_RDS_STATS(COUNTER)                                                                     \
  COUNTER(config_reload)                                                                           \
  COUNTER(update_attempt)                                                                          \
  COUNTER(update_success)                                                                          \
  COUNTER(update_failure)
// clang-format on

/**
 * Struct definition for all RDS stats. @see stats_macros.h
 */
struct RdsStats {
  ALL_RDS_STATS(GENERATE_COUNTER_STRUCT)
};

class RouteConfigProviderManagerImpl;

/**
 * Implementation of RouteConfigProvider that fetches the route configuration dynamically using
 * the RDS API.
 */
class RdsRouteConfigProviderImpl : public RouteConfigProvider,
                                   public Init::Target,
                                   Http::RestApiFetcher,
                                   Logger::Loggable<Logger::Id::router> {
public:
  ~RdsRouteConfigProviderImpl();

  // Init::Target
  void initialize(std::function<void()> callback) override {
    initialize_callback_ = callback;
    RestApiFetcher::initialize();
  }

  // Router::RouteConfigProvider
  Router::ConfigConstSharedPtr config() override;

  // Http::RestApiFetcher
  void createRequest(Http::Message& request) override;
  void parseResponse(const Http::Message& response) override;
  void onFetchComplete() override;
  void onFetchFailure(const EnvoyException* e) override;

private:
  struct ThreadLocalConfig : public ThreadLocal::ThreadLocalObject {
    ThreadLocalConfig(ConfigConstSharedPtr initial_config) : config_(initial_config) {}

    ConfigConstSharedPtr config_;
  };

  RdsRouteConfigProviderImpl(const Json::Object& config, Runtime::Loader& runtime,
                             Upstream::ClusterManager& cm, Event::Dispatcher& dispatcher,
                             Runtime::RandomGenerator& random,
                             const LocalInfo::LocalInfo& local_info, Stats::Scope& scope,
                             const std::string& stat_prefix, ThreadLocal::SlotAllocator& tls,
                             RouteConfigProviderManagerImpl& route_config_provider_manager);
  void registerInitTarget(Init::Manager& init_manager);

  Runtime::Loader& runtime_;
  const LocalInfo::LocalInfo& local_info_;
  ThreadLocal::SlotPtr tls_;
  const std::string route_config_name_;
  bool initialized_{};
  uint64_t last_config_hash_{};
  RdsStats stats_;
  std::function<void()> initialize_callback_;
  RouteConfigProviderManagerImpl& route_config_provider_manager_;

  friend class RouteConfigProviderManagerImpl;
};

class RouteConfigProviderManagerImpl : public ServerRouteConfigProviderManager {
public:
  RouteConfigProviderManagerImpl(Runtime::Loader& runtime, Event::Dispatcher& dispatcher,
                                 Runtime::RandomGenerator& random,
                                 const LocalInfo::LocalInfo& local_info,
                                 ThreadLocal::SlotAllocator& tls);
  ~RouteConfigProviderManagerImpl() {}

  // ServerRouteConfigProviderManager
  std::vector<RouteConfigProviderSharedPtr> routeConfigProviders() override;
  // RouteConfigProviderManager
  RouteConfigProviderSharedPtr getRouteConfigProvider(const Json::Object& config,
                                                      Upstream::ClusterManager& cm,
                                                      Stats::Scope& scope,
                                                      const std::string& stat_prefix,
                                                      Init::Manager& init_manager) override;

private:
  std::unordered_map<std::string, std::weak_ptr<RouteConfigProvider>> route_config_providers_;
  Runtime::Loader& runtime_;
  Event::Dispatcher& dispatcher_;
  Runtime::RandomGenerator& random_;
  const LocalInfo::LocalInfo& local_info_;
  ThreadLocal::SlotAllocator& tls_;

  friend class RdsRouteConfigProviderImpl;
};

} // namespace Router
} // namespace Envoy
