#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>

#include "envoy/config/subscription.h"
#include "envoy/init/init.h"
#include "envoy/local_info/local_info.h"
#include "envoy/router/rds.h"
#include "envoy/router/route_config_provider_manager.h"
#include "envoy/thread_local/thread_local.h"

#include "common/common/logger.h"

#include "api/filter/http_connection_manager.pb.h"
#include "api/rds.pb.h"

namespace Envoy {
namespace Router {

/**
 * Route configuration provider utilities.
 */
class RouteConfigProviderUtil {
public:
  /**
   * @return RouteConfigProviderPtr a new route configuration provider based on the supplied proto
   *         configuration.
   */
  static RouteConfigProviderSharedPtr
  create(const envoy::api::v2::filter::HttpConnectionManager& config, Runtime::Loader& runtime,
         Upstream::ClusterManager& cm, Stats::Scope& scope, const std::string& stat_prefix,
         Init::Manager& init_manager, RouteConfigProviderManager& route_config_provider_manager);
};

/**
 * Implementation of RouteConfigProvider that holds a static route configuration.
 */
class StaticRouteConfigProviderImpl : public RouteConfigProvider {
public:
  StaticRouteConfigProviderImpl(const envoy::api::v2::RouteConfiguration& config,
                                Runtime::Loader& runtime, Upstream::ClusterManager& cm);

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
  COUNTER(config_reload)

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
class RdsRouteConfigProviderImpl
    : public RouteConfigProvider,
      public Init::Target,
      Envoy::Config::SubscriptionCallbacks<envoy::api::v2::RouteConfiguration>,
      Logger::Loggable<Logger::Id::router> {
public:
  ~RdsRouteConfigProviderImpl();

  // Init::Target
  void initialize(std::function<void()> callback) override {
    initialize_callback_ = callback;
    subscription_->start({route_config_name_}, *this);
  }

  // Router::RouteConfigProvider
  Router::ConfigConstSharedPtr config() override;

  // Config::SubscriptionCallbacks
  void onConfigUpdate(const ResourceVector& resources) override;
  void onConfigUpdateFailed(const EnvoyException* e) override;

private:
  struct ThreadLocalConfig : public ThreadLocal::ThreadLocalObject {
    ThreadLocalConfig(ConfigConstSharedPtr initial_config) : config_(initial_config) {}

    ConfigConstSharedPtr config_;
  };

  RdsRouteConfigProviderImpl(const envoy::api::v2::filter::Rds& rds,
                             const std::string& manager_identifier, Runtime::Loader& runtime,
                             Upstream::ClusterManager& cm, Event::Dispatcher& dispatcher,
                             Runtime::RandomGenerator& random,
                             const LocalInfo::LocalInfo& local_info, Stats::Scope& scope,
                             const std::string& stat_prefix, ThreadLocal::SlotAllocator& tls,
                             RouteConfigProviderManagerImpl& route_config_provider_manager);
  void registerInitTarget(Init::Manager& init_manager);
  void runInitializeCallbackIfAny();

  Runtime::Loader& runtime_;
  Upstream::ClusterManager& cm_;
  std::unique_ptr<Envoy::Config::Subscription<envoy::api::v2::RouteConfiguration>> subscription_;
  ThreadLocal::SlotPtr tls_;
  const std::string route_config_name_;
  bool initialized_{};
  uint64_t last_config_hash_{};
  Stats::ScopePtr scope_;
  RdsStats stats_;
  std::function<void()> initialize_callback_;
  RouteConfigProviderManagerImpl& route_config_provider_manager_;
  const std::string manager_identifier_;

  friend class RouteConfigProviderManagerImpl;
};

// TODO(junr03): use then singleton manager in #1410.
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
  RouteConfigProviderSharedPtr getRouteConfigProvider(const envoy::api::v2::filter::Rds& rds,
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
