#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>

#include "envoy/api/v2/rds.pb.h"
#include "envoy/api/v2/route/route.pb.h"
#include "envoy/config/filter/network/http_connection_manager/v2/http_connection_manager.pb.h"
#include "envoy/config/subscription.h"
#include "envoy/http/codes.h"
#include "envoy/init/init.h"
#include "envoy/local_info/local_info.h"
#include "envoy/router/rds.h"
#include "envoy/router/route_config_provider_manager.h"
#include "envoy/server/admin.h"
#include "envoy/singleton/instance.h"
#include "envoy/thread_local/thread_local.h"

#include "common/common/logger.h"
#include "common/protobuf/utility.h"

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
  create(const envoy::config::filter::network::http_connection_manager::v2::HttpConnectionManager&
             config,
         Runtime::Loader& runtime, Upstream::ClusterManager& cm, Stats::Scope& scope,
         const std::string& stat_prefix, Init::Manager& init_manager,
         RouteConfigProviderManager& route_config_provider_manager);
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
  const std::string versionInfo() const override { CONSTRUCT_ON_FIRST_USE(std::string, "static"); }

private:
  ConfigConstSharedPtr config_;
};

/**
 * All RDS stats. @see stats_macros.h
 */
// clang-format off
#define ALL_RDS_STATS(COUNTER)                                                                     \
  COUNTER(config_reload)                                                                           \
  COUNTER(update_empty)

// clang-format on

/**
 * Struct definition for all RDS stats. @see stats_macros.h
 */
struct RdsStats {
  ALL_RDS_STATS(GENERATE_COUNTER_STRUCT)
};

class RouteConfigProviderManagerImpl;

/**
 * Implementation of RdsRouteConfigProvider that fetches the route configuration dynamically using
 * the RDS API.
 */
class RdsRouteConfigProviderImpl
    : public RdsRouteConfigProvider,
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

  // Router::RdsRouteConfigProvider
  std::string configAsJson() const override {
    return MessageUtil::getJsonStringFromMessage(route_config_proto_, true);
  }
  const std::string& routeConfigName() const override { return route_config_name_; }
  const std::string& configSource() const override { return config_source_; }
  const std::string versionInfo() const override { return subscription_->versionInfo(); }

  // Config::SubscriptionCallbacks
  void onConfigUpdate(const ResourceVector& resources) override;
  void onConfigUpdateFailed(const EnvoyException* e) override;

private:
  struct ThreadLocalConfig : public ThreadLocal::ThreadLocalObject {
    ThreadLocalConfig(ConfigConstSharedPtr initial_config) : config_(initial_config) {}

    ConfigConstSharedPtr config_;
  };

  RdsRouteConfigProviderImpl(
      const envoy::config::filter::network::http_connection_manager::v2::Rds& rds,
      const std::string& manager_identifier, Runtime::Loader& runtime, Upstream::ClusterManager& cm,
      Event::Dispatcher& dispatcher, Runtime::RandomGenerator& random,
      const LocalInfo::LocalInfo& local_info, Stats::Scope& scope, const std::string& stat_prefix,
      ThreadLocal::SlotAllocator& tls,
      RouteConfigProviderManagerImpl& route_config_provider_manager);

  void registerInitTarget(Init::Manager& init_manager);
  void runInitializeCallbackIfAny();

  Runtime::Loader& runtime_;
  Upstream::ClusterManager& cm_;
  std::unique_ptr<Envoy::Config::Subscription<envoy::api::v2::RouteConfiguration>> subscription_;
  ThreadLocal::SlotPtr tls_;
  std::string config_source_;
  const std::string route_config_name_;
  bool initialized_{};
  uint64_t last_config_hash_{};
  Stats::ScopePtr scope_;
  RdsStats stats_;
  std::function<void()> initialize_callback_;
  RouteConfigProviderManagerImpl& route_config_provider_manager_;
  const std::string manager_identifier_;
  envoy::api::v2::RouteConfiguration route_config_proto_;

  friend class RouteConfigProviderManagerImpl;
};

class RouteConfigProviderManagerImpl : public ServerRouteConfigProviderManager,
                                       public Singleton::Instance {
public:
  RouteConfigProviderManagerImpl(Runtime::Loader& runtime, Event::Dispatcher& dispatcher,
                                 Runtime::RandomGenerator& random,
                                 const LocalInfo::LocalInfo& local_info,
                                 ThreadLocal::SlotAllocator& tls, Server::Admin& admin);
  ~RouteConfigProviderManagerImpl();

  // ServerRouteConfigProviderManager
  std::vector<RdsRouteConfigProviderSharedPtr> rdsRouteConfigProviders() override;
  // RouteConfigProviderManager
  RouteConfigProviderSharedPtr getRouteConfigProvider(
      const envoy::config::filter::network::http_connection_manager::v2::Rds& rds,
      Upstream::ClusterManager& cm, Stats::Scope& scope, const std::string& stat_prefix,
      Init::Manager& init_manager) override;

private:
  /**
   * The handler used in the Admin /routes endpoint. This handler is used to
   * populate the response Buffer::Instance with information about the currently
   * loaded dynamic HTTP Route Tables.
   * @param path_and_query supplies the url path and query-params sent to the Admin endpoint.
   * @param response_headers enables setting of http headers (eg content-type, cache-control) in
   * the handler.
   * @param response supplies the buffer to fill with information.
   * @return Http::Code OK if the endpoint can parse and operate on the url, NotFound otherwise.
   */
  Http::Code handlerRoutes(const std::string& path_and_query, Http::HeaderMap& response_headers,
                           Buffer::Instance& response);

  /**
   * Helper function used by handlerRoutes. The function loops through the providers
   * and adds them to the response.
   * @param response supplies the buffer to fill with information.
   * @param providers supplies the vector of providers to add to the response.
   * @return Http::Code OK.
   */
  Http::Code handlerRoutesLoop(Buffer::Instance& response,
                               const std::vector<RdsRouteConfigProviderSharedPtr> providers);

  std::unordered_map<std::string, std::weak_ptr<RdsRouteConfigProviderImpl>>
      route_config_providers_;
  Runtime::Loader& runtime_;
  Event::Dispatcher& dispatcher_;
  Runtime::RandomGenerator& random_;
  const LocalInfo::LocalInfo& local_info_;
  ThreadLocal::SlotAllocator& tls_;
  Server::Admin& admin_;

  friend class RdsRouteConfigProviderImpl;
};

} // namespace Router
} // namespace Envoy
