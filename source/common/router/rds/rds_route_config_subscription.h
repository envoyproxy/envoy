#pragma once

#include <memory>
#include <string>

#include "envoy/config/core/v3/config_source.pb.h"
#include "envoy/router/rds/route_config_provider.h"
#include "envoy/router/rds/route_config_update_receiver.h"
#include "envoy/server/factory_context.h"
#include "envoy/stats/scope.h"

#include "source/common/common/logger.h"
#include "source/common/config/subscription_base.h"
#include "source/common/grpc/common.h"
#include "source/common/init/manager_impl.h"
#include "source/common/init/target_impl.h"
#include "source/common/init/watcher_impl.h"
#include "source/common/router/rds/route_config_provider_manager.h"

#include "absl/types/optional.h"

namespace Envoy {
namespace Router {
namespace Rds {

/**
 * All RDS stats. @see stats_macros.h
 */
#define ALL_RDS_STATS(COUNTER, GAUGE)                                                              \
  COUNTER(config_reload)                                                                           \
  COUNTER(update_empty)                                                                            \
  GAUGE(config_reload_time_ms, NeverImport)

/**
 * Struct definition for all RDS stats. @see stats_macros.h
 */
struct RdsStats {
  ALL_RDS_STATS(GENERATE_COUNTER_STRUCT, GENERATE_GAUGE_STRUCT)
};

/**
 * A class that fetches the route configuration dynamically using the RDS API and updates them to
 * RDS config providers.
 */
template <class RouteConfiguration, class Config>
class RdsRouteConfigSubscription : Envoy::Config::SubscriptionBase<RouteConfiguration>,
                                   protected Logger::Loggable<Logger::Id::router> {
public:
  RdsRouteConfigSubscription(
      RouteConfigUpdatePtr<RouteConfiguration, Config>&& config_update,
      const envoy::config::core::v3::ConfigSource& config_source,
      const std::string& route_config_name, const uint64_t manager_identifier,
      Server::Configuration::ServerFactoryContext& factory_context, const std::string& stat_prefix,
      RouteConfigProviderManager<RouteConfiguration, Config>& route_config_provider_manager)
      : Envoy::Config::SubscriptionBase<RouteConfiguration>(
            factory_context.messageValidationContext().dynamicValidationVisitor(), "name"),
        route_config_name_(route_config_name),
        scope_(
            factory_context.scope().createScope(stat_prefix + "rds." + route_config_name_ + ".")),
        factory_context_(factory_context),
        parent_init_target_(fmt::format("RdsRouteConfigSubscription init {}", route_config_name_),
                            [this]() { local_init_manager_.initialize(local_init_watcher_); }),
        local_init_watcher_(fmt::format("RDS local-init-watcher {}", route_config_name_),
                            [this]() { parent_init_target_.ready(); }),
        local_init_target_(
            fmt::format("RdsRouteConfigSubscription local-init-target {}", route_config_name_),
            [this]() { subscription_->start({route_config_name_}); }),
        local_init_manager_(fmt::format("RDS local-init-manager {}", route_config_name_)),
        stat_prefix_(stat_prefix),
        stats_({ALL_RDS_STATS(POOL_COUNTER(*scope_), POOL_GAUGE(*scope_))}),
        route_config_provider_manager_(route_config_provider_manager),
        manager_identifier_(manager_identifier), config_update_info_(std::move(config_update)) {
    const auto resource_name =
        Envoy::Config::SubscriptionBase<RouteConfiguration>::getResourceName();
    subscription_ =
        factory_context.clusterManager().subscriptionFactory().subscriptionFromConfigSource(
            config_source, Envoy::Grpc::Common::typeUrl(resource_name), *scope_, *this,
            Envoy::Config::SubscriptionBase<RouteConfiguration>::resource_decoder_, {});
    local_init_manager_.add(local_init_target_);
  }

  ~RdsRouteConfigSubscription() override {
    // If we get destroyed during initialization, make sure we signal that we "initialized".
    local_init_target_.ready();

    // The ownership of RdsRouteConfigProviderImpl is shared among all HttpConnectionManagers that
    // hold a shared_ptr to it. The RouteConfigProviderManager holds weak_ptrs to the
    // RdsRouteConfigProviders. Therefore, the map entry for the RdsRouteConfigProvider has to get
    // cleaned by the RdsRouteConfigProvider's destructor.
    route_config_provider_manager_.eraseDynamicProvider(manager_identifier_);
  }

  absl::optional<RouteConfigProvider<RouteConfiguration, Config>*>& routeConfigProvider() {
    return route_config_provider_opt_;
  }
  RouteConfigUpdatePtr<RouteConfiguration, Config>& routeConfigUpdate() {
    return config_update_info_;
  }

  const Init::Target& initTarget() { return parent_init_target_; }

private:
  // Config::SubscriptionCallbacks
  void onConfigUpdate(const std::vector<Envoy::Config::DecodedResourceRef>& resources,
                      const std::string& version_info) override {
    if (!validateUpdateSize(resources.size())) {
      return;
    }
    const auto& route_config =
        dynamic_cast<const RouteConfiguration&>(resources[0].get().resource());
    if (route_config.name() != route_config_name_) {
      // check_format.py complains about throw, dummy comment helps to ignore
      /**/ throw EnvoyException(fmt::format("Unexpected RDS configuration (expecting {}): {}",
                                          route_config_name_, route_config.name()));
    }
    if (route_config_provider_opt_.has_value()) {
      route_config_provider_opt_.value()->validateConfig(route_config);
    }
    if (config_update_info_->onRdsUpdate(route_config, version_info)) {
      stats_.config_reload_.inc();
      stats_.config_reload_time_ms_.set(DateUtil::nowToMilliseconds(factory_context_.timeSource()));

      beforeProviderUpdate();

      ENVOY_LOG(debug, "rds: loading new configuration: config_name={} hash={}", route_config_name_,
                config_update_info_->configHash());

      if (route_config_provider_opt_.has_value()) {
        route_config_provider_opt_.value()->onConfigUpdate();
      }

      afterProviderUpdate();
    }

    local_init_target_.ready();
  }

  void onConfigUpdate(const std::vector<Envoy::Config::DecodedResourceRef>& added_resources,
                      const Protobuf::RepeatedPtrField<std::string>& removed_resources,
                      const std::string&) override {
    if (!removed_resources.empty()) {
      // TODO(#2500) when on-demand resource loading is supported, an RDS removal may make sense
      // (see discussion in #6879), and so we should do something other than ignoring here.
      ENVOY_LOG(
          error,
          "Server sent a delta RDS update attempting to remove a resource (name: {}). Ignoring.",
          removed_resources[0]);
    }
    if (!added_resources.empty()) {
      onConfigUpdate(added_resources, added_resources[0].get().version());
    }
  }

  void onConfigUpdateFailed(Envoy::Config::ConfigUpdateFailureReason reason,
                            const EnvoyException*) override {
    ASSERT(Envoy::Config::ConfigUpdateFailureReason::ConnectionFailure != reason);
    // We need to allow server startup to continue, even if we have a bad
    // config.
    local_init_target_.ready();
  }

  bool validateUpdateSize(int num_resources) {
    if (num_resources == 0) {
      ENVOY_LOG(debug, "Missing RouteConfiguration for {} in onConfigUpdate()", route_config_name_);
      stats_.update_empty_.inc();
      local_init_target_.ready();
      return false;
    }
    if (num_resources != 1) {
      /**/ throw EnvoyException(fmt::format("Unexpected RDS resource length: {}", num_resources));
      // (would be a return false here)
    }
    return true;
  }

  virtual void beforeProviderUpdate() {}
  virtual void afterProviderUpdate() {}

protected:
  const std::string route_config_name_;
  // This scope must outlive the subscription_ below as the subscription has derived stats.
  Stats::ScopePtr scope_;
  Envoy::Config::SubscriptionPtr subscription_;
  Server::Configuration::ServerFactoryContext& factory_context_;

  // Init target used to notify the parent init manager that the subscription [and its sub resource]
  // is ready.
  Init::SharedTargetImpl parent_init_target_;
  // Init watcher on RDS and VHDS ready event. This watcher marks parent_init_target_ ready.
  Init::WatcherImpl local_init_watcher_;
  // Target which starts the RDS subscription.
  Init::TargetImpl local_init_target_;
  Init::ManagerImpl local_init_manager_;
  std::string stat_prefix_;
  RdsStats stats_;
  RouteConfigProviderManager<RouteConfiguration, Config>& route_config_provider_manager_;
  const uint64_t manager_identifier_;
  absl::optional<RouteConfigProvider<RouteConfiguration, Config>*> route_config_provider_opt_;
  RouteConfigUpdatePtr<RouteConfiguration, Config> config_update_info_;
};

template <class RouteConfiguration, class Config>
using RdsRouteConfigSubscriptionSharedPtr =
    std::shared_ptr<RdsRouteConfigSubscription<RouteConfiguration, Config>>;

} // namespace Rds
} // namespace Router
} // namespace Envoy
