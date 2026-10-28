#pragma once

#include <list>
#include <string>

#include "envoy/config/route/v3/route.pb.h"
#include "envoy/config/route/v3/route_components.pb.h"
#include "envoy/http/filter.h"
#include "envoy/rds/config_traits.h"
#include "envoy/router/rds.h"
#include "envoy/router/route_config_update_receiver.h"
#include "envoy/server/factory_context.h"
#include "envoy/service/discovery/v3/discovery.pb.h"

#include "source/common/common/logger.h"
#include "source/common/protobuf/utility.h"
#include "source/common/rds/route_config_update_receiver_impl.h"
#include "source/common/router/config_impl.h"
#include "source/common/router/vhds.h"

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"

namespace Envoy {
namespace Router {

class ConfigTraitsImpl : public Rds::ConfigTraits {
public:
  ConfigTraitsImpl(ProtobufMessage::ValidationVisitor& validator) : validator_(validator) {}

  Rds::ConfigConstSharedPtr createNullConfig() const override;
  Rds::ConfigConstSharedPtr createConfig(const Protobuf::Message& rc,
                                         Server::Configuration::ServerFactoryContext& context,
                                         Init::Manager&,
                                         bool validate_clusters_default) const override;

private:
  ProtobufMessage::ValidationVisitor& validator_;
};

struct UpdateOnDemandCallback {
  const std::string alias_;
  Event::Dispatcher& thread_local_dispatcher_;
  std::weak_ptr<Http::RouteConfigUpdatedCallback> cb_;
};

class RouteConfigUpdateReceiverImpl : public RouteConfigUpdateReceiver,
                                      public VhdsConfigUpdateReceiver {
public:
  RouteConfigUpdateReceiverImpl(Rds::ProtoTraits& proto_traits,
                                Server::Configuration::ServerFactoryContext& factory_context,
                                const std::string& stat_prefix, bool from_rds)
      : config_traits_(factory_context.messageValidationContext().dynamicValidationVisitor()),
        base_(config_traits_, proto_traits, factory_context), factory_context_(factory_context),
        stat_prefix_(stat_prefix), from_rds_(from_rds) {}
  ~RouteConfigUpdateReceiverImpl() override {
    base_.warmer_.setObserver({});
    base_.warmer_.abortWarming();
    vhds_subscription_.reset();
  }

  using VirtualHostMap = std::map<std::string, envoy::config::route::v3::VirtualHost>;

  bool removeVhosts(VirtualHostMap& vhosts,
                    const Protobuf::RepeatedPtrField<std::string>& removed_vhost_names);
  bool updateVhosts(VirtualHostMap& vhosts, const VirtualHostRefVector& added_vhosts);

  // Router::RouteConfigUpdateReceiver
  absl::Status onRdsUpdate(const Protobuf::Message& rc, const std::string& version_info) override;
  bool onVhdsUpdate(const VirtualHostRefVector& added_vhosts,
                    std::set<std::string>&& added_resource_ids,
                    const Protobuf::RepeatedPtrField<std::string>& removed_resources,
                    const std::string& version_info) override;
  void onRdsFailure() override { base_.onRdsFailure(); }
  void setObserver(OptRef<Rds::RouteConfigUpdateObserver> observer) override {
    base_.warmer_.setObserver(observer);
  }
  bool configWarming() const override { return base_.warmer_.warming(); }
  uint64_t configHash() const override { return base_.configHash(); }
  const std::optional<Rds::RouteConfigProvider::ConfigInfo>& configInfo() const override {
    return base_.configInfo();
  }
  Rds::ConfigConstSharedPtr parsedConfiguration() const override {
    return base_.parsedConfiguration();
  }
  SystemTime lastUpdated() const override { return base_.lastUpdated(); }
  void requestVirtualHostsUpdate(
      const std::string& for_domain, Event::Dispatcher& thread_local_dispatcher,
      std::weak_ptr<Http::RouteConfigUpdatedCallback> route_config_updated_cb) override;
  void onVhdsConfigPublished() override;

private:
  enum class OnDemandAliasState {
    WaitingForResponse,
    Warming,
    Published,
  };

  const std::string& routeConfigName() const {
    return !base_.warming_state_.route_config_name_.empty()
               ? base_.warming_state_.route_config_name_
               : base_.route_config_name_;
  }
  bool isAliasPublished(const std::string& alias) const {
    const auto it = on_demand_aliases_.find(alias);
    return it != on_demand_aliases_.end() && it->second == OnDemandAliasState::Published;
  }

  static void notifyCallback(const ConfigImpl& config, const std::string& alias,
                             Event::Dispatcher& thread_local_dispatcher,
                             std::weak_ptr<Http::RouteConfigUpdatedCallback> cb);

  const Protobuf::Any& latestRouteConfigurationAny() const {
    return base_.warming_state_.route_config_any_.has_value()
               ? *base_.warming_state_.route_config_any_
               : base_.route_config_any_;
  }

  absl::StatusOr<VhdsSubscriptionPtr>
  createVhdsSubscription(const envoy::config::route::v3::RouteConfiguration& route_config,
                         Init::Manager& init_manager);

  ConfigTraitsImpl config_traits_;

  Rds::RouteConfigUpdateReceiverImpl base_;
  Server::Configuration::ServerFactoryContext& factory_context_;
  // The parent prefix alone, for example 'http.<stat_prefix>.'. A VHDS subscription of a route
  // configuration delivered over RDS is nested under that route configuration's own 'rds.'
  // namespace, which `from_rds_` selects.
  const std::string stat_prefix_;
  const bool from_rds_ = false;
  // The VHDS subscription of the currently published route configuration, if it configures VHDS.
  // It is created and replaced by onRdsUpdate(), which is where the per-update init manager that
  // its initial fetch warms up with lives.
  VhdsSubscriptionPtr vhds_subscription_;
  uint64_t last_vhds_config_hash_{0ul};

  // vhosts supplied by RDS, to be merged with VHDS vhosts in onVhdsUpdate.
  std::unique_ptr<VirtualHostMap> rds_virtual_hosts_;
  // vhosts supplied by VHDS, to be merged with RDS vhosts in onRdsUpdate.
  std::unique_ptr<VirtualHostMap> vhds_virtual_hosts_;
  // The resource IDs explicitly requested on demand from the current subscription, and their
  // state in the on-demand lifecycle.
  absl::flat_hash_map<std::string, OnDemandAliasState> on_demand_aliases_;
  std::list<UpdateOnDemandCallback> pending_on_demand_callbacks_;
  // A flag used to determine if this instance of RouteConfigUpdateReceiverImpl hasn't been
  // deallocated. Please also see a comment in requestVirtualHostsUpdate() method implementation.
  std::shared_ptr<bool> still_alive_{std::make_shared<bool>(true)};
};

} // namespace Router
} // namespace Envoy
