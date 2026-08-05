#pragma once

#include <cstdint>
#include <functional>
#include <string>

#include "envoy/config/core/v3/config_source.pb.h"
#include "envoy/config/route/v3/route_components.pb.h"
#include "envoy/config/route/v3/route_components.pb.validate.h"
#include "envoy/config/subscription.h"
#include "envoy/http/codes.h"
#include "envoy/local_info/local_info.h"
#include "envoy/router/rds.h"
#include "envoy/router/route_config_update_receiver.h"
#include "envoy/server/filter_config.h"
#include "envoy/service/discovery/v3/discovery.pb.h"
#include "envoy/singleton/instance.h"
#include "envoy/stats/scope.h"
#include "envoy/thread_local/thread_local.h"

#include "source/common/common/logger.h"
#include "source/common/config/resource_type_helper.h"
#include "source/common/init/manager_impl.h"
#include "source/common/init/target_impl.h"
#include "source/common/init/watcher_impl.h"
#include "source/common/protobuf/utility.h"

#include "absl/container/node_hash_set.h"

namespace Envoy {
namespace Router {

#define ALL_VHDS_STATS(COUNTER)                                                                    \
  COUNTER(config_reload)                                                                           \
  COUNTER(update_empty)

struct VhdsStats {
  ALL_VHDS_STATS(GENERATE_COUNTER_STRUCT)
};

class VhdsSubscription : public Envoy::Config::SubscriptionCallbacks,
                         Logger::Loggable<Logger::Id::router> {
public:
  static absl::StatusOr<std::unique_ptr<VhdsSubscription>>
  createVhdsSubscription(RouteConfigUpdatePtr& config_update_info,
                         Server::Configuration::ServerFactoryContext& factory_context,
                         const std::string& stat_prefix,
                         Rds::RouteConfigProvider* route_config_provider);

  ~VhdsSubscription() override { init_target_.ready(); }

  void registerInitTargetWithInitManager(Init::Manager& m) { m.add(init_target_); }
  void updateOnDemand(const std::string& with_route_config_name_prefix);
  static std::string domainNameToAlias(const std::string& route_config_name,
                                       const std::string& domain) {
    return route_config_name + "/" + domain;
  }
  static std::string aliasToDomainName(const std::string& alias) {
    const auto pos = alias.find_last_of('/');
    return pos == std::string::npos ? alias : alias.substr(pos + 1);
  }

private:
  VhdsSubscription(RouteConfigUpdatePtr& config_update_info,
                   Server::Configuration::ServerFactoryContext& factory_context,
                   const std::string& stat_prefix, Rds::RouteConfigProvider* route_config_provider,
                   absl::Status& creation_status);

  // Config::SubscriptionCallbacks
  absl::Status onConfigUpdate(const std::vector<Envoy::Config::DecodedResourceRef>&,
                              const std::string&) override {
    return absl::OkStatus();
  }
  absl::Status onConfigUpdate(const std::vector<Envoy::Config::DecodedResourceRef>&,
                              const Protobuf::RepeatedPtrField<std::string>&,
                              const std::string&) override;
  void onConfigUpdateFailed(Envoy::Config::ConfigUpdateFailureReason reason,
                            const EnvoyException* e) override;

  // Takes ownership of the init manager that is dedicated to the route configuration of the VHDS
  // update that is being applied and starts warming it up. If a previous update is still warming up
  // it is abandoned, as this update supersedes it. Only called once the update is known to be
  // applied, so that an update that turns out to be a no-op leaves a warming up update alone.
  void commitUpdateInitManager(std::unique_ptr<Init::ManagerImpl> update_init_manager,
                               absl::string_view version_info);
  // Drops the per-update init manager. Called once the route configuration of the update has been
  // warmed up and published, so that there is no init manager in between updates.
  void resetUpdateInitManager();
  // Called when everything that registered to the per-update init manager is warmed up. Publishes
  // the new route configuration and signals that this subscription is ready.
  void onUpdateInitManagerReady();

  RouteConfigUpdatePtr& config_update_info_;
  Stats::ScopeSharedPtr scope_;
  VhdsStats stats_;
  Envoy::Config::SubscriptionPtr subscription_;
  Init::TargetImpl init_target_;
  // Init manager that is used to warm up the resources that are owned by the route configuration
  // built by the VHDS update that is currently being processed. Every update gets its own
  // independent init manager, and it is reset once the new route configuration is warmed up and
  // published, so it's null in between updates.
  std::unique_ptr<Init::ManagerImpl> update_init_manager_;
  // Watcher that update_init_manager_ notifies once everything it warms up is ready.
  std::unique_ptr<Init::WatcherImpl> update_init_watcher_;
  // Result of the last publishing attempt. Publishing is deferred until the route configuration is
  // warmed up, so a failure is only reported back to the xDS layer in the common case where there
  // is nothing to warm up and the publishing therefore happens synchronously. It also gates
  // whether this subscription signals readiness, so that nothing is told a route configuration is
  // ready when publishing it failed.
  absl::Status publish_status_;
  const Envoy::Config::ResourceTypeHelper<envoy::config::route::v3::VirtualHost>
      resource_type_helper_;

  Rds::RouteConfigProvider* route_config_provider_;
};

using VhdsSubscriptionPtr = std::unique_ptr<VhdsSubscription>;

} // namespace Router
} // namespace Envoy
