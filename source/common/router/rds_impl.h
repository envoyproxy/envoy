#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "envoy/admin/v2alpha/config_dump.pb.h"
#include "envoy/api/v2/core/config_source.pb.h"
#include "envoy/api/v2/discovery.pb.h"
#include "envoy/api/v2/rds.pb.h"
#include "envoy/config/filter/network/http_connection_manager/v2/http_connection_manager.pb.h"
#include "envoy/config/subscription.h"
#include "envoy/http/codes.h"
#include "envoy/local_info/local_info.h"
#include "envoy/router/rds.h"
#include "envoy/router/route_config_provider_manager.h"
#include "envoy/router/route_config_update_receiver.h"
#include "envoy/server/admin.h"
#include "envoy/server/filter_config.h"
#include "envoy/singleton/instance.h"
#include "envoy/stats/scope.h"
#include "envoy/thread_local/thread_local.h"

#include "common/common/callback_impl.h"
#include "common/common/cleanup.h"
#include "common/common/logger.h"
#include "common/config/resources.h"
#include "common/init/manager_impl.h"
#include "common/init/target_impl.h"
#include "common/init/watcher_impl.h"
#include "common/protobuf/utility.h"
#include "common/router/route_config_update_receiver_impl.h"
#include "common/router/vhds.h"

namespace Envoy {
namespace Router {

// For friend class declaration in RdsRouteConfigSubscription.
class ScopedRdsConfigSubscription;

/**
 * Route configuration provider utilities.
 */
class RouteConfigProviderUtil {
public:
  /**
   * @return RouteConfigProviderSharedPtr a new route configuration provider based on the supplied
   * proto configuration. Notes the provider object could be shared among multiple listeners.
   */
  static RouteConfigProviderSharedPtr
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
  ~StaticRouteConfigProviderImpl() override;

  // Router::RouteConfigProvider
  Router::ConfigConstSharedPtr config() override { return config_; }
  absl::optional<ConfigInfo> configInfo() const override {
    return ConfigInfo{route_config_proto_, ""};
  }
  SystemTime lastUpdated() const override { return last_updated_; }
  void onConfigUpdate() override {}
  void validateConfig(const envoy::api::v2::RouteConfiguration&) const override {}

private:
  ConfigConstSharedPtr config_;
  envoy::api::v2::RouteConfiguration route_config_proto_;
  SystemTime last_updated_;
  RouteConfigProviderManagerImpl& route_config_provider_manager_;
};

/**
 * All RDS stats. @see stats_macros.h
 */
#define ALL_RDS_STATS(COUNTER)                                                                     \
  COUNTER(config_reload)                                                                           \
  COUNTER(update_empty)

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
class RdsRouteConfigSubscription : Envoy::Config::SubscriptionCallbacks,
                                   Logger::Loggable<Logger::Id::router> {
public:
  ~RdsRouteConfigSubscription() override;

  std::unordered_set<RouteConfigProvider*>& routeConfigProviders() {
    ASSERT(route_config_providers_.size() == 1 || route_config_providers_.empty());
    return route_config_providers_;
  }
  RouteConfigUpdatePtr& routeConfigUpdate() { return config_update_info_; }
  void maybeCreateInitManager(const std::string& version_info,
                              std::unique_ptr<Init::ManagerImpl>& init_manager,
                              std::unique_ptr<Cleanup>& resume_rds);

private:
  // Config::SubscriptionCallbacks
  void onConfigUpdate(const Protobuf::RepeatedPtrField<ProtobufWkt::Any>& resources,
                      const std::string& version_info) override;
  void onConfigUpdate(const Protobuf::RepeatedPtrField<envoy::api::v2::Resource>& added_resources,
                      const Protobuf::RepeatedPtrField<std::string>& removed_resources,
                      const std::string&) override;
  void onConfigUpdateFailed(Envoy::Config::ConfigUpdateFailureReason reason,
                            const EnvoyException* e) override;
  std::string resourceName(const ProtobufWkt::Any& resource) override {
    return MessageUtil::anyConvert<envoy::api::v2::RouteConfiguration>(resource).name();
  }

  Common::CallbackHandle* addUpdateCallback(std::function<void()> callback) {
    return update_callback_manager_.add(callback);
  }

  RdsRouteConfigSubscription(
      const envoy::config::filter::network::http_connection_manager::v2::Rds& rds,
      const uint64_t manager_identifier,
      Server::Configuration::ServerFactoryContext& factory_context,
      ProtobufMessage::ValidationVisitor& validator, Init::Manager& init_manager,
      const std::string& stat_prefix, RouteConfigProviderManagerImpl& route_config_provider_manager,
      const envoy::api::v2::core::ConfigSource::XdsApiVersion xds_api_version =
          envoy::api::v2::core::ConfigSource::AUTO);

  bool validateUpdateSize(int num_resources);
  std::string loadTypeUrl();

  Init::Manager& getRdsConfigInitManager() { return init_manager_; }

  std::unique_ptr<Envoy::Config::Subscription> subscription_;
  const std::string route_config_name_;
  Server::Configuration::ServerFactoryContext& factory_context_;
  ProtobufMessage::ValidationVisitor& validator_;
  Init::Manager& init_manager_;
  Init::SharedTargetImpl init_target_;
  Stats::ScopePtr scope_;
  std::string stat_prefix_;
  RdsStats stats_;
  RouteConfigProviderManagerImpl& route_config_provider_manager_;
  const uint64_t manager_identifier_;
  // TODO(lambdai): Prove that a subscription has exactly one provider and remove the container.
  std::unordered_set<RouteConfigProvider*> route_config_providers_;
  VhdsSubscriptionPtr vhds_subscription_;
  RouteConfigUpdatePtr config_update_info_;
  Common::CallbackManager<> update_callback_manager_;
  envoy::api::v2::core::ConfigSource::XdsApiVersion xds_api_version_;

  friend class RouteConfigProviderManagerImpl;
  // Access to addUpdateCallback
  friend class ScopedRdsConfigSubscription;
};

using RdsRouteConfigSubscriptionSharedPtr = std::shared_ptr<RdsRouteConfigSubscription>;

/**
 * Implementation of RouteConfigProvider that fetches the route configuration dynamically using
 * the subscription.
 */
class RdsRouteConfigProviderImpl : public RouteConfigProvider,
                                   Logger::Loggable<Logger::Id::router> {
public:
  ~RdsRouteConfigProviderImpl() override;

  RdsRouteConfigSubscription& subscription() { return *subscription_; }

  // Router::RouteConfigProvider
  Router::ConfigConstSharedPtr config() override;
  absl::optional<ConfigInfo> configInfo() const override {
    return config_update_info_->configInfo();
  }
  SystemTime lastUpdated() const override { return config_update_info_->lastUpdated(); }
  void onConfigUpdate() override;
  void validateConfig(const envoy::api::v2::RouteConfiguration& config) const override;

private:
  struct ThreadLocalConfig : public ThreadLocal::ThreadLocalObject {
    ThreadLocalConfig(ConfigConstSharedPtr initial_config) : config_(std::move(initial_config)) {}
    ConfigConstSharedPtr config_;
  };

  RdsRouteConfigProviderImpl(RdsRouteConfigSubscriptionSharedPtr&& subscription,
                             Server::Configuration::FactoryContext& factory_context);

  RdsRouteConfigSubscriptionSharedPtr subscription_;
  RouteConfigUpdatePtr& config_update_info_;
  Server::Configuration::ServerFactoryContext& factory_context_;
  ProtobufMessage::ValidationVisitor& validator_;
  ThreadLocal::SlotPtr tls_;

  friend class RouteConfigProviderManagerImpl;
};

class RouteConfigProviderManagerImpl : public RouteConfigProviderManager,
                                       public Singleton::Instance {
public:
  RouteConfigProviderManagerImpl(Server::Admin& admin);

  std::unique_ptr<envoy::admin::v2alpha::RoutesConfigDump> dumpRouteConfigs() const;

  // RouteConfigProviderManager
  RouteConfigProviderSharedPtr createRdsRouteConfigProvider(
      const envoy::config::filter::network::http_connection_manager::v2::Rds& rds,
      Server::Configuration::FactoryContext& factory_context, const std::string& stat_prefix,
      Init::Manager& init_manager) override;

  RouteConfigProviderPtr
  createStaticRouteConfigProvider(const envoy::api::v2::RouteConfiguration& route_config,
                                  Server::Configuration::FactoryContext& factory_context) override;

private:
  // TODO(jsedgwick) These two members are prime candidates for the owned-entry list/map
  // as in ConfigTracker. I.e. the ProviderImpls would have an EntryOwner for these lists
  // Then the lifetime management stuff is centralized and opaque.
  std::unordered_map<uint64_t, std::weak_ptr<RdsRouteConfigProviderImpl>>
      dynamic_route_config_providers_;
  std::unordered_set<RouteConfigProvider*> static_route_config_providers_;
  Server::ConfigTracker::EntryOwnerPtr config_tracker_entry_;

  friend class RdsRouteConfigSubscription;
  friend class StaticRouteConfigProviderImpl;
};

} // namespace Router
} // namespace Envoy
