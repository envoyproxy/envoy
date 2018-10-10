#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "envoy/admin/v2alpha/config_dump.pb.h"
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
#include "envoy/server/filter_config.h"
#include "envoy/singleton/instance.h"
#include "envoy/stats/scope.h"
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
  static RouteConfigProviderPtr
  create(const envoy::config::filter::network::http_connection_manager::v2::HttpConnectionManager&
             config,
         Server::Configuration::FactoryContext& factory_context, const std::string& stat_prefix,
         RouteConfigProviderManager& route_config_provider_manager);
};

class RouteConfigProviderManagerImpl;

/**
 * Implementation of RouteConfigProvider that holds a static route configuration.
 */
class StaticRouteConfigProviderImpl : public RouteConfigProvider {
public:
  StaticRouteConfigProviderImpl(const envoy::api::v2::RouteConfiguration& config,
                                Server::Configuration::FactoryContext& factory_context,
                                RouteConfigProviderManagerImpl& route_config_provider_manager);
  ~StaticRouteConfigProviderImpl();

  // Router::RouteConfigProvider
  Router::ConfigConstSharedPtr config() override { return config_; }
  absl::optional<ConfigInfo> configInfo() const override {
    return ConfigInfo{route_config_proto_, ""};
  }
  SystemTime lastUpdated() const override { return last_updated_; }

private:
  ConfigConstSharedPtr config_;
  envoy::api::v2::RouteConfiguration route_config_proto_;
  SystemTime last_updated_;
  RouteConfigProviderManagerImpl& route_config_provider_manager_;
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

class RdsRouteConfigProviderImpl;

/**
 * A class that fetches the route configuration dynamically using the RDS API and updates them to
 * RDS config providers.
 */
class RdsRouteConfigSubscription
    : public Init::Target,
      Envoy::Config::SubscriptionCallbacks<envoy::api::v2::RouteConfiguration>,
      Logger::Loggable<Logger::Id::router> {
public:
  ~RdsRouteConfigSubscription();

  // Init::Target
  void initialize(std::function<void()> callback) override {
    initialize_callback_ = callback;
    subscription_->start({route_config_name_}, *this);
  }

  // Config::SubscriptionCallbacks
  void onConfigUpdate(const ResourceVector& resources, const std::string& version_info) override;
  void onConfigUpdateFailed(const EnvoyException* e) override;
  std::string resourceName(const ProtobufWkt::Any& resource) override {
    return MessageUtil::anyConvert<envoy::api::v2::RouteConfiguration>(resource).name();
  }

private:
  struct LastConfigInfo {
    uint64_t last_config_hash_;
    std::string last_config_version_;
  };

  RdsRouteConfigSubscription(
      const envoy::config::filter::network::http_connection_manager::v2::Rds& rds,
      const std::string& manager_identifier, Server::Configuration::FactoryContext& factory_context,
      const std::string& stat_prefix,
      RouteConfigProviderManagerImpl& route_config_provider_manager);

  void registerInitTarget(Init::Manager& init_manager);
  void runInitializeCallbackIfAny();

  std::unique_ptr<Envoy::Config::Subscription<envoy::api::v2::RouteConfiguration>> subscription_;
  std::function<void()> initialize_callback_;
  const std::string route_config_name_;
  Stats::ScopePtr scope_;
  RdsStats stats_;
  RouteConfigProviderManagerImpl& route_config_provider_manager_;
  const std::string manager_identifier_;
  TimeSource& time_source_;
  SystemTime last_updated_;
  absl::optional<LastConfigInfo> config_info_;
  envoy::api::v2::RouteConfiguration route_config_proto_;
  std::unordered_set<RdsRouteConfigProviderImpl*> route_config_providers_;

  friend class RouteConfigProviderManagerImpl;
  friend class RdsRouteConfigProviderImpl;
};

typedef std::shared_ptr<RdsRouteConfigSubscription> RdsRouteConfigSubscriptionSharedPtr;

/**
 * Implementation of RouteConfigProvider that fetches the route configuration dynamically using
 * the subscription.
 */
class RdsRouteConfigProviderImpl : public RouteConfigProvider,
                                   Logger::Loggable<Logger::Id::router> {
public:
  ~RdsRouteConfigProviderImpl();

  RdsRouteConfigSubscription& subscription() { return *subscription_; }
  void onConfigUpdate();

  // Router::RouteConfigProvider
  Router::ConfigConstSharedPtr config() override;
  absl::optional<ConfigInfo> configInfo() const override;
  SystemTime lastUpdated() const override { return subscription_->last_updated_; }

private:
  struct ThreadLocalConfig : public ThreadLocal::ThreadLocalObject {
    ThreadLocalConfig(ConfigConstSharedPtr initial_config) : config_(initial_config) {}

    ConfigConstSharedPtr config_;
  };

  RdsRouteConfigProviderImpl(RdsRouteConfigSubscriptionSharedPtr&& subscription,
                             Server::Configuration::FactoryContext& factory_context);

  RdsRouteConfigSubscriptionSharedPtr subscription_;
  Server::Configuration::FactoryContext& factory_context_;
  ThreadLocal::SlotPtr tls_;

  friend class RouteConfigProviderManagerImpl;
};

class RouteConfigProviderManagerImpl : public RouteConfigProviderManager,
                                       public Singleton::Instance {
public:
  RouteConfigProviderManagerImpl(Server::Admin& admin);

  std::unique_ptr<envoy::admin::v2alpha::RoutesConfigDump> dumpRouteConfigs() const;

  // RouteConfigProviderManager
  RouteConfigProviderPtr createRdsRouteConfigProvider(
      const envoy::config::filter::network::http_connection_manager::v2::Rds& rds,
      Server::Configuration::FactoryContext& factory_context,
      const std::string& stat_prefix) override;

  RouteConfigProviderPtr
  createStaticRouteConfigProvider(const envoy::api::v2::RouteConfiguration& route_config,
                                  Server::Configuration::FactoryContext& factory_context) override;

private:
  // TODO(jsedgwick) These two members are prime candidates for the owned-entry list/map
  // as in ConfigTracker. I.e. the ProviderImpls would have an EntryOwner for these lists
  // Then the lifetime management stuff is centralized and opaque.
  std::unordered_map<std::string, std::weak_ptr<RdsRouteConfigSubscription>>
      route_config_subscriptions_;
  std::unordered_set<RouteConfigProvider*> static_route_config_providers_;
  Server::ConfigTracker::EntryOwnerPtr config_tracker_entry_;

  friend class RdsRouteConfigSubscription;
  friend class StaticRouteConfigProviderImpl;
};

} // namespace Router
} // namespace Envoy
