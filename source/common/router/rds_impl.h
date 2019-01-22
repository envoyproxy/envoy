#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <queue>

#include "envoy/admin/v2alpha/config_dump.pb.h"
#include "envoy/api/v2/rds.pb.h"
#include "envoy/api/v2/route/route.pb.h"
#include "envoy/config/filter/network/http_connection_manager/v2/http_connection_manager.pb.h"
#include "envoy/config/subscription.h"
#include "envoy/http/codes.h"
#include "envoy/local_info/local_info.h"
#include "envoy/router/rds.h"
#include "envoy/router/route_config_provider_manager.h"
#include "envoy/server/admin.h"
#include "envoy/server/filter_config.h"
#include "envoy/singleton/instance.h"
#include "envoy/stats/scope.h"
#include "envoy/thread_local/thread_local.h"

#include "common/common/logger.h"
#include "common/init/target_impl.h"
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
  bool requestConfigUpdate(const std::string, std::function<void()>) override { return false; }

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

// clang-format off
#define ALL_VHDS_STATS(COUNTER)                                                                     \
  COUNTER(config_reload)                                                                           \
  COUNTER(update_empty)

// clang-format on

struct VhdsStats {
  ALL_VHDS_STATS(GENERATE_COUNTER_STRUCT)
};

struct LastConfigInfo {
  uint64_t last_config_hash_;
  std::string last_config_version_;
};

class RdsRouteConfigProviderImpl;
class VhdsSubscription;
typedef std::unique_ptr<VhdsSubscription> VhdsSubscriptionPtr;

/**
 * A class that fetches the route configuration dynamically using the RDS API and updates them to
 * RDS config providers.
 */
class RdsRouteConfigSubscription : Envoy::Config::SubscriptionCallbacks,
                                   Logger::Loggable<Logger::Id::router> {
public:
  ~RdsRouteConfigSubscription() override;

  // Config::SubscriptionCallbacks
  // TODO(fredlas) deduplicate
  void onConfigUpdate(const Protobuf::RepeatedPtrField<ProtobufWkt::Any>& resources,
                      const std::string& version_info) override;
  void onConfigUpdate(const Protobuf::RepeatedPtrField<envoy::api::v2::Resource>&,
                      const Protobuf::RepeatedPtrField<std::string>&, const std::string&) override {
    NOT_IMPLEMENTED_GCOVR_EXCL_LINE;
  }
  void onConfigUpdateFailed(const EnvoyException* e) override;
  std::string resourceName(const ProtobufWkt::Any& resource) override {
    return MessageUtil::anyConvert<envoy::api::v2::RouteConfiguration>(resource).name();
  }
  // TODO: add provider registration call
  void ondemandUpdate(const std::vector<std::string>& aliases);
  std::unordered_set<RdsRouteConfigProviderImpl*>& route_config_providers() { return route_config_providers_; }
  absl::optional<LastConfigInfo> configInfo() const;
  envoy::api::v2::RouteConfiguration& routeConfiguration();
  SystemTime lastUpdated() const;

private:
  RdsRouteConfigSubscription(
      const envoy::config::filter::network::http_connection_manager::v2::Rds& rds,
      const uint64_t manager_identifier, Server::Configuration::FactoryContext& factory_context,
      const std::string& stat_prefix,
      RouteConfigProviderManagerImpl& route_config_provider_manager);

  std::unique_ptr<Envoy::Config::Subscription> subscription_;
  Server::Configuration::FactoryContext& factory_context_;
  const std::string route_config_name_;
  Init::TargetImpl init_target_;
  Stats::ScopePtr scope_;
  std::string stat_prefix_;
  RdsStats stats_;
  RouteConfigProviderManagerImpl& route_config_provider_manager_;
  const uint64_t manager_identifier_;
  TimeSource& time_source_;
  SystemTime last_updated_;
  absl::optional<LastConfigInfo> config_info_;
  envoy::api::v2::RouteConfiguration route_config_proto_;
  std::unordered_set<RdsRouteConfigProviderImpl*> route_config_providers_;
  VhdsSubscriptionPtr vhds_subscription_;
  bool uses_vhds_;

  friend class RouteConfigProviderManagerImpl;
  friend class RdsRouteConfigProviderImpl;
};

typedef std::shared_ptr<RdsRouteConfigSubscription> RdsRouteConfigSubscriptionSharedPtr;

class VhdsSubscription : Envoy::Config::SubscriptionCallbacks<envoy::api::v2::route::VirtualHost>,
                         Logger::Loggable<Logger::Id::router> {
public:
  VhdsSubscription(
          const envoy::api::v2::RouteConfiguration& route_configuration,
          Server::Configuration::FactoryContext& factory_context,
          const std::string& stat_prefix, RdsRouteConfigSubscription* rds_subscription);
  ~VhdsSubscription() {   init_target_.ready(); }

  // Config::SubscriptionCallbacks
  // TODO(fredlas) deduplicate
  void onConfigUpdate(const ResourceVector&, const std::string&) override {
    NOT_IMPLEMENTED_GCOVR_EXCL_LINE;
  }
  void onConfigUpdate(const Protobuf::RepeatedPtrField<envoy::api::v2::Resource>&,
                      const Protobuf::RepeatedPtrField<std::string>&, const std::string&) override;
  void onConfigUpdateFailed(const EnvoyException* e) override;
  std::string resourceName(const ProtobufWkt::Any& resource) override {
    return MessageUtil::anyConvert<envoy::api::v2::route::VirtualHost>(resource).name();
  }

  void registerInitTargetWithInitManager(Init::Manager& m) { m.add(init_target_); }
  void ondemandUpdate(const std::vector<std::string>& aliases);
  void remove_vhosts(std::unordered_map<std::string, envoy::api::v2::route::VirtualHost>& vhosts,
                     const Protobuf::RepeatedPtrField<std::string>& removed_vhost_names);
  void update_vhosts(std::unordered_map<std::string, envoy::api::v2::route::VirtualHost>& vhosts,
                     const Protobuf::RepeatedPtrField<envoy::api::v2::Resource>& added_resources);
  void rebuild_route_config(
          const std::unordered_map<std::string, envoy::api::v2::route::VirtualHost>& vhosts,
          envoy::api::v2::RouteConfiguration& route_config);
  absl::optional<LastConfigInfo> configInfo() const { return config_info_; }
  envoy::api::v2::RouteConfiguration& routeConfiguration() { return route_config_proto_; }
  SystemTime lastUpdated() const { return last_updated_; }

  std::unique_ptr<Envoy::Config::Subscription<envoy::api::v2::route::VirtualHost>> subscription_;
  envoy::api::v2::RouteConfiguration route_config_proto_;
  const std::string route_config_name_;
  Init::TargetImpl init_target_;
  Stats::ScopePtr scope_;
  VhdsStats stats_;
  TimeSource& time_source_;
  SystemTime last_updated_;
  RdsRouteConfigSubscription* rds_subscription_;
  std::unordered_map<std::string, envoy::api::v2::route::VirtualHost> virtual_hosts_;
  absl::optional<LastConfigInfo> config_info_;
};

struct ThreadLocalConfig : public ThreadLocal::ThreadLocalObject {
  ThreadLocalConfig(ConfigConstSharedPtr initial_config) : config_(initial_config) {}
  ConfigConstSharedPtr config_;
};

struct ThreadLocalCallbacks : public ThreadLocal::ThreadLocalObject {
  std::queue<std::function<void()>> callbacks_;
};

/**
 * Implementation of RouteConfigProvider that fetches the route configuration dynamically using
 * the subscription.
 */
class RdsRouteConfigProviderImpl : public RouteConfigProvider,
                                   Logger::Loggable<Logger::Id::router> {
public:
  ~RdsRouteConfigProviderImpl();

  void onConfigUpdate();

  // Router::RouteConfigProvider
  Router::ConfigConstSharedPtr config() override;
  absl::optional<ConfigInfo> configInfo() const override;
  SystemTime lastUpdated() const override { return subscription_->lastUpdated(); }
  bool requestConfigUpdate(const std::string for_domain, std::function<void()> cb) override;

private:
  RdsRouteConfigProviderImpl(RdsRouteConfigSubscriptionSharedPtr&& subscription,
                             Server::Configuration::FactoryContext& factory_context);

  void addConfigUpdateCallback(std::function<void()> cb);

  RdsRouteConfigSubscriptionSharedPtr subscription_;
  Server::Configuration::FactoryContext& factory_context_;
  SystemTime last_updated_;
  ThreadLocal::SlotPtr tls_;
  ThreadLocal::SlotPtr config_update_callbacks_;

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
  std::unordered_map<uint64_t, std::weak_ptr<RdsRouteConfigSubscription>>
      route_config_subscriptions_;
  std::unordered_set<RouteConfigProvider*> static_route_config_providers_;
  Server::ConfigTracker::EntryOwnerPtr config_tracker_entry_;

  friend class RdsRouteConfigSubscription;
  friend class StaticRouteConfigProviderImpl;
};

} // namespace Router
} // namespace Envoy
