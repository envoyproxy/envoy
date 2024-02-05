#pragma once

#include <memory>
#include <string>

#include "envoy/common/callback.h"
#include "envoy/config/core/v3/config_source.pb.h"
#include "envoy/config/route/v3/scoped_route.pb.h"
#include "envoy/config/route/v3/scoped_route.pb.validate.h"
#include "envoy/config/subscription.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"
#include "envoy/router/route_config_provider_manager.h"
#include "envoy/router/scopes.h"
#include "envoy/service/discovery/v3/discovery.pb.h"
#include "envoy/stats/scope.h"

#include "source/common/config/config_provider_impl.h"
#include "source/common/config/subscription_base.h"
#include "source/common/init/manager_impl.h"
#include "source/common/router/rds_impl.h"
#include "source/common/router/scoped_config_impl.h"

namespace Envoy {
namespace Router {

// Scoped routing configuration utilities.
namespace ScopedRoutesConfigProviderUtil {

// If enabled in the HttpConnectionManager config, returns a ConfigProvider for scoped routing
// configuration.
Envoy::Config::ConfigProviderPtr create(
    const envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
        config,
    Server::Configuration::ServerFactoryContext& factory_context, Init::Manager& init_manager,
    const std::string& stat_prefix,
    Envoy::Config::ConfigProviderManager& scoped_routes_config_provider_manager);

// If enabled in the HttpConnectionManager config, returns a ConfigProvider for scoped routing
// configuration.
ScopeKeyBuilderPtr createScopeKeyBuilder(
    const envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
        config);

} // namespace ScopedRoutesConfigProviderUtil

class ScopedRoutesConfigProviderManager;

// A ConfigProvider for inline scoped routing configuration.
// InlineScopedRoutesConfigProvider is not fully implemented at this point. It doesn't load
// ScopedRouteConfigurations and propagate them to worker threads. If
// InlineScopedRoutesConfigProvider is fully implemented, when it is loading
// ScopedRouteConfiguration, the on demand field should be ignored and all scopes should be loaded
// eagerly.
class InlineScopedRoutesConfigProvider : public Envoy::Config::ImmutableConfigProviderBase {
public:
  InlineScopedRoutesConfigProvider(ProtobufTypes::ConstMessagePtrVector&& config_protos,
                                   std::string name,
                                   Server::Configuration::ServerFactoryContext& factory_context,
                                   ScopedRoutesConfigProviderManager& config_provider_manager,
                                   envoy::config::core::v3::ConfigSource rds_config_source);

  ~InlineScopedRoutesConfigProvider() override = default;

  const std::string& name() const { return name_; }

  // Envoy::Config::ConfigProvider
  Envoy::Config::ConfigProvider::ConfigProtoVector getConfigProtos() const override {
    Envoy::Config::ConfigProvider::ConfigProtoVector out_protos;
    std::for_each(scopes_.begin(), scopes_.end(),
                  [&out_protos](const ScopedRouteInfoConstSharedPtr& scope) {
                    out_protos.push_back(&scope->configProto());
                  });
    return out_protos;
  }

  std::string getConfigVersion() const override { return ""; }
  ConfigConstSharedPtr getConfig() const override { return config_; }

private:
  const std::string name_;
  const std::vector<ScopedRouteInfoConstSharedPtr> scopes_;
  ConfigConstSharedPtr config_;
  const envoy::config::core::v3::ConfigSource rds_config_source_;
};

/**
 * All SRDS stats. @see stats_macros.h
 */
// clang-format off
#define ALL_SCOPED_RDS_STATS(COUNTER, GAUGE)                                                       \
  COUNTER(config_reload)                                                                           \
  COUNTER(update_empty)                                                                            \
  GAUGE(all_scopes, Accumulate)                                                                    \
  GAUGE(config_reload_time_ms, NeverImport)                                                        \
  GAUGE(on_demand_scopes, Accumulate)                                                              \
  GAUGE(active_scopes, Accumulate)

// clang-format on

struct ScopedRdsStats {
  ALL_SCOPED_RDS_STATS(GENERATE_COUNTER_STRUCT, GENERATE_GAUGE_STRUCT)

  static ScopedRdsStats generateStats(const std::string& prefix, Stats::Scope& scope) {
    return ScopedRdsStats{
        ALL_SCOPED_RDS_STATS(POOL_COUNTER_PREFIX(scope, prefix), POOL_GAUGE_PREFIX(scope, prefix))};
  }
};

// A scoped RDS subscription to be used with the dynamic scoped RDS ConfigProvider.
class ScopedRdsConfigSubscription
    : public Envoy::Config::DeltaConfigSubscriptionInstance,
      public Envoy::Config::SubscriptionBase<envoy::config::route::v3::ScopedRouteConfiguration> {
public:
  using ScopedRouteConfigurationMap =
      std::map<std::string, envoy::config::route::v3::ScopedRouteConfiguration>;

  ScopedRdsConfigSubscription(
      const envoy::extensions::filters::network::http_connection_manager::v3::ScopedRds& scoped_rds,
      const uint64_t manager_identifier, const std::string& name,
      Server::Configuration::ServerFactoryContext& factory_context, const std::string& stat_prefix,
      envoy::config::core::v3::ConfigSource rds_config_source,
      RouteConfigProviderManager& route_config_provider_manager,
      ScopedRoutesConfigProviderManager& config_provider_manager);

  ~ScopedRdsConfigSubscription() override = default;

  const std::string& name() const { return name_; }

  const ScopedRouteMap& scopedRouteMap() const { return scoped_route_map_; }

  void
  onDemandRdsUpdate(std::shared_ptr<Router::ScopeKey> scope_key,
                    Event::Dispatcher& thread_local_dispatcher,
                    Http::RouteConfigUpdatedCallback&& route_config_updated_cb,
                    std::weak_ptr<Envoy::Config::ConfigSubscriptionCommonBase> weak_subscription);

private:
  // A helper class that takes care of the life cycle management of a RDS route provider and the
  // update callback handle.
  struct RdsRouteConfigProviderHelper {
    RdsRouteConfigProviderHelper(
        ScopedRdsConfigSubscription& parent, std::string scope_name,
        envoy::extensions::filters::network::http_connection_manager::v3::Rds& rds,
        Init::Manager& init_manager);

    RdsRouteConfigProviderHelper(ScopedRdsConfigSubscription& parent, std::string scope_name);

    ~RdsRouteConfigProviderHelper() {
      // Only remove the rds update when the rds provider has been initialized.
      if (route_provider_) {
        parent_.stats_.active_scopes_.dec();
      }
      if (on_demand_) {
        parent_.stats_.on_demand_scopes_.dec();
      }
    }
    ConfigConstSharedPtr routeConfig() { return route_provider_->configCast(); }

    void addOnDemandUpdateCallback(std::function<void()> callback);

    // Runs all the callback from worker thread to continue filter chain.
    void runOnDemandUpdateCallback();

    // If route provider has not been initialized, initialize it.
    void maybeInitRdsConfigProvider();

    // Initialize route provider and register for rds update.
    void initRdsConfigProvider(
        envoy::extensions::filters::network::http_connection_manager::v3::Rds& rds,
        Init::Manager& init_manager);

    ScopedRdsConfigSubscription& parent_;
    std::string scope_name_;
    bool on_demand_;
    RdsRouteConfigProviderImplSharedPtr route_provider_;
    // This handle_ is owned by the route config provider's RDS subscription, when the helper
    // destructs, the handle is deleted as well.
    Common::CallbackHandlePtr rds_update_callback_handle_;
    std::vector<std::function<void()>> on_demand_update_callbacks_;
  };

  using RdsRouteConfigProviderHelperPtr = std::unique_ptr<RdsRouteConfigProviderHelper>;

  // Adds or updates scopes, create a new RDS provider for each resource, if an exception is thrown
  // during updating, the exception message is collected via the exception messages vector.
  // Returns a failed status if the operation was unsuccessful. If successful,
  // returns a boolean indicating if any scopes were applied.
  absl::StatusOr<bool>
  addOrUpdateScopes(const std::vector<Envoy::Config::DecodedResourceRef>& resources,
                    Init::Manager& init_manager, const std::string& version_info);
  // Removes given scopes from the managed set of scopes.
  // Returns a list of to be removed helpers which is temporally held in the onConfigUpdate method,
  // to make sure new scopes sharing the same RDS source configs could reuse the subscriptions.
  std::list<RdsRouteConfigProviderHelperPtr>
  removeScopes(const Protobuf::RepeatedPtrField<std::string>& scope_names,
               const std::string& version_info);

  // Envoy::Config::DeltaConfigSubscriptionInstance
  void start() override { subscription_->start({}); }

  // Detect scope name and scope key conflict between added scopes or between added scopes and old
  // scopes. Some removed scopes may be in added resources list, instead of being removed, they
  // should be updated, so only return scope names that will disappear after update. If conflict
  // detected, fill exception_msg with information about scope conflict and return.
  Protobuf::RepeatedPtrField<std::string> detectUpdateConflictAndCleanupRemoved(
      const std::vector<Envoy::Config::DecodedResourceRef>& added_resources,
      const Protobuf::RepeatedPtrField<std::string>& removed_resources, std::string& exception_msg);

  // Envoy::Config::SubscriptionCallbacks

  // NOTE: both delta form and state-of-the-world form onConfigUpdate(resources, version_info) will
  // throw an EnvoyException or return failure on any error and essentially reject an update.
  absl::Status onConfigUpdate(const std::vector<Envoy::Config::DecodedResourceRef>& resources,
                              const std::string& version_info) override;
  absl::Status onConfigUpdate(const std::vector<Envoy::Config::DecodedResourceRef>& added_resources,
                              const Protobuf::RepeatedPtrField<std::string>& removed_resources,
                              const std::string& system_version_info) override;
  void onConfigUpdateFailed(Envoy::Config::ConfigUpdateFailureReason reason,
                            const EnvoyException*) override {
    ASSERT(Envoy::Config::ConfigUpdateFailureReason::ConnectionFailure != reason);
    DeltaConfigSubscriptionInstance::onConfigUpdateFailed();
  }
  // Propagate RDS updates to ScopeConfigImpl in workers.
  void onRdsConfigUpdate(const std::string& scope_name, ConfigConstSharedPtr new_rds_config);

  // ScopedRouteInfo by scope name.
  ScopedRouteMap scoped_route_map_;

  // For creating RDS subscriptions.
  Server::Configuration::ServerFactoryContext& factory_context_;
  const std::string name_;
  // Stats must outlive subscription.
  Stats::ScopeSharedPtr scope_;
  ScopedRdsStats stats_;
  Envoy::Config::SubscriptionPtr subscription_;
  const envoy::config::core::v3::ConfigSource rds_config_source_;
  const std::string stat_prefix_;
  RouteConfigProviderManager& route_config_provider_manager_;

  // RdsRouteConfigProvider by scope name.
  absl::flat_hash_map<std::string, RdsRouteConfigProviderHelperPtr> route_provider_by_scope_;
  // A map of (hash, scope-name), used to detect the key conflict between scopes.
  absl::flat_hash_map<uint64_t, std::string> scope_name_by_hash_;
};

using ScopedRdsConfigSubscriptionSharedPtr = std::shared_ptr<ScopedRdsConfigSubscription>;

// A ConfigProvider for scoped RDS that dynamically fetches scoped routing configuration via a
// subscription.
class ScopedRdsConfigProvider : public Envoy::Config::MutableConfigProviderCommonBase {
public:
  ScopedRdsConfigProvider(ScopedRdsConfigSubscriptionSharedPtr&& subscription);

  ScopedRdsConfigSubscription& subscription() const {
    return *static_cast<ScopedRdsConfigSubscription*>(subscription_.get());
  }
  void onDemandRdsUpdate(std::shared_ptr<Router::ScopeKey> scope_key,
                         Event::Dispatcher& thread_local_dispatcher,
                         Http::RouteConfigUpdatedCallback&& route_config_updated_cb) const {
    subscription().onDemandRdsUpdate(
        std::move(scope_key), thread_local_dispatcher, std::move(route_config_updated_cb),
        std::weak_ptr<Envoy::Config::ConfigSubscriptionCommonBase>(subscription_));
  }
};

// A ConfigProviderManager for scoped routing configuration that creates static/inline and dynamic
// (xds) config providers.
class ScopedRoutesConfigProviderManager : public Envoy::Config::ConfigProviderManagerImplBase {
public:
  ScopedRoutesConfigProviderManager(
      OptRef<Server::Admin> admin,
      Router::RouteConfigProviderManager& route_config_provider_manager)
      : Envoy::Config::ConfigProviderManagerImplBase(admin, "route_scopes"),
        route_config_provider_manager_(route_config_provider_manager) {}

  ~ScopedRoutesConfigProviderManager() override = default;

  // Envoy::Config::ConfigProviderManagerImplBase
  ProtobufTypes::MessagePtr dumpConfigs(const Matchers::StringMatcher& name_matcher) const override;

  // Envoy::Config::ConfigProviderManager
  Envoy::Config::ConfigProviderPtr
  createXdsConfigProvider(const Protobuf::Message& config_source_proto,
                          Server::Configuration::ServerFactoryContext& factory_context,
                          Init::Manager& init_manager, const std::string& stat_prefix,
                          const Envoy::Config::ConfigProviderManager::OptionalArg& optarg) override;
  Envoy::Config::ConfigProviderPtr
  createStaticConfigProvider(const Protobuf::Message&, Server::Configuration::ServerFactoryContext&,
                             const Envoy::Config::ConfigProviderManager::OptionalArg&) override {
    PANIC("SRDS supports delta updates and requires the use of the createStaticConfigProvider() "
          "overload that accepts a config proto set as an argument.");
  }
  Envoy::Config::ConfigProviderPtr createStaticConfigProvider(
      std::vector<std::unique_ptr<const Protobuf::Message>>&& config_protos,
      Server::Configuration::ServerFactoryContext& factory_context,
      const Envoy::Config::ConfigProviderManager::OptionalArg& optarg) override;

  RouteConfigProviderManager& routeConfigProviderManager() {
    return route_config_provider_manager_;
  }

private:
  RouteConfigProviderManager& route_config_provider_manager_;
};

using ScopedRoutesConfigProviderManagerPtr = std::unique_ptr<ScopedRoutesConfigProviderManager>;
using ScopedRoutesConfigProviderManagerSharedPtr =
    std::shared_ptr<ScopedRoutesConfigProviderManager>;

// The optional argument passed to the ConfigProviderManager::create*() functions.
class ScopedRoutesConfigProviderManagerOptArg
    : public Envoy::Config::ConfigProviderManager::OptionalArg {
public:
  ScopedRoutesConfigProviderManagerOptArg(
      std::string scoped_routes_name,
      const envoy::config::core::v3::ConfigSource& rds_config_source)
      : scoped_routes_name_(std::move(scoped_routes_name)), rds_config_source_(rds_config_source) {}

  const std::string scoped_routes_name_;
  const envoy::config::core::v3::ConfigSource& rds_config_source_;
};

} // namespace Router
} // namespace Envoy
