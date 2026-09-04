#pragma once

#include <memory>
#include <optional>
#include <string>

#include "envoy/config/core/v3/config_source.pb.h"
#include "envoy/config/subscription.h"
#include "envoy/rds/route_config_provider.h"
#include "envoy/rds/route_config_update_receiver.h"
#include "envoy/server/factory_context.h"
#include "envoy/stats/scope.h"

#include "source/common/grpc/common.h"
#include "source/common/init/manager_impl.h"
#include "source/common/init/target_impl.h"
#include "source/common/init/watcher_impl.h"
#include "source/common/rds/route_config_provider_manager.h"

namespace Envoy {
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
class RdsRouteConfigSubscription : Envoy::Config::SubscriptionCallbacks,
                                   RouteConfigUpdateObserver,
                                   protected Logger::Loggable<Logger::Id::rds> {
public:
  static absl::StatusOr<std::unique_ptr<RdsRouteConfigSubscription>>
  create(RouteConfigUpdatePtr&& config_update,
         Envoy::Config::OpaqueResourceDecoderSharedPtr&& resource_decoder,
         const envoy::config::core::v3::ConfigSource& config_source,
         const std::string& route_config_name, const uint64_t manager_identifier,
         Server::Configuration::ServerFactoryContext& factory_context,
         const std::string& stat_prefix, const std::string& rds_type,
         RouteConfigProviderManager& route_config_provider_manager);

  ~RdsRouteConfigSubscription() override;

  RouteConfigProvider*& routeConfigProvider() { return route_config_provider_; }

  RouteConfigUpdatePtr& routeConfigUpdate() { return config_update_info_; }

  const Init::Target& initTarget() { return parent_init_target_; }

protected:
  RdsRouteConfigSubscription(RouteConfigUpdatePtr&& config_update,
                             Envoy::Config::OpaqueResourceDecoderSharedPtr&& resource_decoder,
                             const envoy::config::core::v3::ConfigSource& config_source,
                             const std::string& route_config_name,
                             const uint64_t manager_identifier,
                             Server::Configuration::ServerFactoryContext& factory_context,
                             const std::string& stat_prefix, const std::string& rds_type,
                             RouteConfigProviderManager& route_config_provider_manager,
                             absl::Status& creation_status);

private:
  // Config::SubscriptionCallbacks
  absl::Status onConfigUpdate(const std::vector<Envoy::Config::DecodedResourceRef>& resources,
                              const std::string& version_info) override;

  absl::Status onConfigUpdate(const std::vector<Envoy::Config::DecodedResourceRef>& added_resources,
                              const Protobuf::RepeatedPtrField<std::string>& removed_resources,
                              const std::string&) override;

  void onConfigUpdateFailed(Envoy::Config::ConfigUpdateFailureReason reason,
                            const EnvoyException*) override;

  // Rds::RouteConfigUpdateObserver
  // Called by the receiver when the route configuration that it built is warmed up. Publishes the
  // new route configuration and signals that this subscription is ready.
  void onConfigWarmed() override;

  // Hooks that a derived subscription uses to react to a warmed up route configuration being
  // published.
  virtual absl::Status beforeProviderUpdate() { return absl::OkStatus(); }
  virtual absl::Status afterProviderUpdate() { return absl::OkStatus(); }

protected:
  const std::string route_config_name_;
  // This scope must outlive the subscription_ below as the subscription has derived stats.
  Stats::ScopeSharedPtr scope_;
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
  // Result of the last publishing attempt. Publishing is deferred until the route configuration is
  // warmed up, so a failure is only reported back to the xDS layer in the common case where there
  // is nothing to warm up and the publishing therefore happens synchronously. It also gates
  // whether this subscription signals readiness, so that nothing is told a route configuration is
  // ready when publishing it failed.
  absl::Status publish_status_;
  const std::string stat_prefix_;
  const std::string rds_type_;
  RdsStats stats_;
  RouteConfigProviderManager& route_config_provider_manager_;
  const uint64_t manager_identifier_;
  RouteConfigProvider* route_config_provider_{nullptr};
  RouteConfigUpdatePtr config_update_info_;
  Envoy::Config::OpaqueResourceDecoderSharedPtr resource_decoder_;
};

using RdsRouteConfigSubscriptionSharedPtr = std::shared_ptr<RdsRouteConfigSubscription>;

} // namespace Rds
} // namespace Envoy
