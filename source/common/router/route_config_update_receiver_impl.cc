#include "source/common/router/route_config_update_receiver_impl.h"

#include <optional>
#include <string>
#include <utility>

#include "envoy/config/route/v3/route.pb.h"
#include "envoy/service/discovery/v3/discovery.pb.h"

#include "source/common/common/assert.h"
#include "source/common/common/fmt.h"
#include "source/common/common/thread.h"
#include "source/common/config/resource_name.h"
#include "source/common/protobuf/utility.h"
#include "source/common/router/config_impl.h"

namespace Envoy {
namespace Router {

namespace {

// Resets 'route_config::virtual_hosts' by merging VirtualHost contained in
// 'rds_vhosts' and 'vhds_vhosts'.
void rebuildRouteConfigVirtualHosts(
    const RouteConfigUpdateReceiverImpl::VirtualHostMap& rds_vhosts,
    const RouteConfigUpdateReceiverImpl::VirtualHostMap& vhds_vhosts,
    envoy::config::route::v3::RouteConfiguration& route_config) {
  route_config.clear_virtual_hosts();
  for (const auto& vhost : rds_vhosts) {
    route_config.mutable_virtual_hosts()->Add()->CheckTypeAndMergeFrom(vhost.second);
  }
  for (const auto& vhost : vhds_vhosts) {
    route_config.mutable_virtual_hosts()->Add()->CheckTypeAndMergeFrom(vhost.second);
  }
}

} // namespace

Rds::ConfigConstSharedPtr ConfigTraitsImpl::createNullConfig() const {
  return std::make_shared<NullConfigImpl>();
}

Rds::ConfigConstSharedPtr
ConfigTraitsImpl::createConfig(const Protobuf::Message& rc,
                               Server::Configuration::ServerFactoryContext& factory_context,
                               Init::Manager& init_manager, bool validate_clusters_default) const {
  ASSERT(Envoy::Protobuf::DynamicCastMessage<envoy::config::route::v3::RouteConfiguration>(&rc));
  return THROW_OR_RETURN_VALUE(
      ConfigImpl::create(static_cast<const envoy::config::route::v3::RouteConfiguration&>(rc),
                         factory_context, validator_, init_manager, validate_clusters_default),
      std::shared_ptr<ConfigImpl>);
}

absl::Status RouteConfigUpdateReceiverImpl::onRdsUpdate(const Protobuf::Message& rc,
                                                        const std::string& version_info) {
  uint64_t new_hash = base_.getHash(rc);
  if (!base_.checkHash(new_hash)) {
    // The route configuration is unchanged, so there is nothing to build, warm up or publish. An
    // update that is still warming up is deliberately left alone.
    return absl::OkStatus();
  }
  auto new_route_config = std::make_unique<envoy::config::route::v3::RouteConfiguration>();
  new_route_config->CheckTypeAndMergeFrom(rc);
  const uint64_t new_vhds_config_hash =
      new_route_config->has_vhds() ? MessageUtil::hash(new_route_config->vhds()) : 0ul;
  std::unique_ptr<VirtualHostMap> rds_virtual_hosts;
  if (new_route_config->has_vhds()) {
    // When using VHDS, stash away RDS vhosts, so that they can be merged with VHDS vhosts in
    // onVhdsUpdate.
    rds_virtual_hosts = std::make_unique<VirtualHostMap>();
    for (const auto& vhost : new_route_config->virtual_hosts()) {
      rds_virtual_hosts->emplace(vhost.name(), vhost);
    }
    if (vhds_virtual_hosts_ != nullptr && !vhds_virtual_hosts_->empty()) {
      // If there are vhosts supplied by VHDS, merge them with RDS vhosts.
      rebuildRouteConfigVirtualHosts(*rds_virtual_hosts, *vhds_virtual_hosts_, *new_route_config);
    }
  }

  std::string update_id = fmt::format("rds {}:{}", new_route_config->name(), version_info);
  // The init manager is kept local until the new route configuration is known to be built without
  // throwing, so that a rejected update leaves a previous update that is still warming up alone.
  auto update_init_manager = base_.warmer_.createInitManager(update_id);
  auto config =
      config_traits_.createConfig(*new_route_config, factory_context_, *update_init_manager,
                                  false /* not validate unknown cluster */);

  // No exception, the route configuration is valid, now we can try to create VHDS if necessary.
  const bool vhds_configuration_changed = new_vhds_config_hash != last_vhds_config_hash_;

  // Captured here because the proto is moved into the state below, before it is used to decide
  // what happens to the VHDS subscription.
  const bool has_vhds = new_route_config->has_vhds();

  std::unique_ptr<Init::Manager> vhds_noop_init_manager;
  std::unique_ptr<Init::Watcher> vhds_noop_init_watcher;
  VhdsSubscriptionPtr new_vhds_subscription;
  if (has_vhds) {
    if (!base_.initialized_) {
      // We are still waiting for the first valid route configuration update but received a
      // different RDS update again. Then always create a new subscription because the previous init
      // manager will be dropped and we need add the VHDS subscription to the new init manager.
      auto subscription_or_error = createVhdsSubscription(*new_route_config, *update_init_manager);
      RETURN_IF_NOT_OK_REF(subscription_or_error.status());
      new_vhds_subscription = std::move(subscription_or_error.value());
    } else if (vhds_configuration_changed || vhds_subscription_ == nullptr) {
      // We have received the first valid route configuration update and the VHDS configuration has
      // changed. Then create a new subscription but use a noop init manager for this subscription
      // to avoid blocking the main init manager.
      // This is for backward compatibility: the previous implementation didn't block the new RDS
      // update if the updated VHDS subscription was not ready.
      vhds_noop_init_manager = std::make_unique<Init::ManagerImpl>(
          fmt::format("VHDS noop init manager for {}", new_route_config->name()));
      vhds_noop_init_watcher = std::make_unique<Init::WatcherImpl>(
          fmt::format("VHDS noop init watcher for {}", new_route_config->name()), []() {});
      auto subscription_or_error =
          createVhdsSubscription(*new_route_config, *vhds_noop_init_manager);
      RETURN_IF_NOT_OK_REF(subscription_or_error.status());
      new_vhds_subscription = std::move(subscription_or_error.value());
    }
    // Otherwise the VHDS configuration is unchanged and its subscription is still around, so that
    // subscription is kept: it is the one that is delivering the virtual hosts of the route
    // configuration that is being updated here.
  }

  // The new route configuration and VHDS subscription have been built without error, now we can
  // update the state and start warming up.
  base_.updateState(std::move(new_route_config), new_hash, version_info, std::move(config),
                    std::move(update_init_manager), std::move(update_id));

  // Now, the state is updated and previous warming update is aborted (if any), we can update the
  // VHDS subscription here. So the destruction of the previous subscription will not bring any
  // side effect.
  if (new_vhds_subscription != nullptr) {
    vhds_subscription_ = std::move(new_vhds_subscription);
  } else if (!has_vhds) {
    // This route configuration doesn't use VHDS, so the subscription of a previous one goes away.
    vhds_subscription_.reset();
  }
  last_vhds_config_hash_ = new_vhds_config_hash;
  rds_virtual_hosts_ = std::move(rds_virtual_hosts);

  base_.startWarming();

  if (vhds_noop_init_manager != nullptr) {
    vhds_noop_init_manager->initialize(*vhds_noop_init_watcher);
  }

  return absl::OkStatus();
}

bool RouteConfigUpdateReceiverImpl::onVhdsUpdate(
    const VirtualHostRefVector& added_vhosts, std::set<std::string>&& added_resource_ids,
    const Protobuf::RepeatedPtrField<std::string>& removed_resources,
    const std::string& version_info) {
  std::unique_ptr<VirtualHostMap> vhosts_after_this_update;
  if (vhds_virtual_hosts_ != nullptr) {
    vhosts_after_this_update = std::make_unique<VirtualHostMap>(*vhds_virtual_hosts_);
  } else {
    vhosts_after_this_update = std::make_unique<VirtualHostMap>();
  }
  if (rds_virtual_hosts_ == nullptr) {
    rds_virtual_hosts_ = std::make_unique<VirtualHostMap>();
  }
  const bool removed = removeVhosts(*vhosts_after_this_update, removed_resources);
  const bool updated = updateVhosts(*vhosts_after_this_update, added_vhosts);

  const bool vhosts_changed = removed || updated || !added_resource_ids.empty();
  if (!vhosts_changed) {
    // Nothing to rebuild, so the currently published route configuration stays in place. An update
    // that is still warming up is deliberately left alone. This update still carried no resource
    // ids, so record that, otherwise the ids of the previous update would be resolved against a
    // later publish.
    resource_ids_in_last_update_ = std::move(added_resource_ids);
    return false;
  }

  auto route_config_after_this_update =
      std::make_unique<envoy::config::route::v3::RouteConfiguration>();
  // Merge the latest RouteConfiguration with the updated VHDS. That is the one an update that is
  // still warming up built, if any, so that this update supersedes it instead of losing it.
  route_config_after_this_update->CheckTypeAndMergeFrom(latestProtobufConfiguration());
  rebuildRouteConfigVirtualHosts(*rds_virtual_hosts_, *vhosts_after_this_update,
                                 *route_config_after_this_update);

  base_.updateConfig(std::move(route_config_after_this_update), std::nullopt, version_info);
  // No exception, the new route configuration is valid, can update the state. This has to happen
  // before the update is published, because publishing runs the on-demand VHDS callbacks against
  // resourceIdsInLastVhdsUpdate().
  vhds_virtual_hosts_ = std::move(vhosts_after_this_update);
  resource_ids_in_last_update_ = std::move(added_resource_ids);
  base_.startWarming();
  return true;
}

bool RouteConfigUpdateReceiverImpl::removeVhosts(
    VirtualHostMap& vhosts, const Protobuf::RepeatedPtrField<std::string>& removed_vhost_names) {
  bool vhosts_removed = false;
  for (const auto& vhost_name : removed_vhost_names) {
    auto found = vhosts.find(vhost_name);
    if (found != vhosts.end()) {
      vhosts_removed = true;
      vhosts.erase(vhost_name);
    }
  }
  return vhosts_removed;
}

bool RouteConfigUpdateReceiverImpl::updateVhosts(VirtualHostMap& vhosts,
                                                 const VirtualHostRefVector& added_vhosts) {
  bool vhosts_added = false;
  for (const auto& vhost : added_vhosts) {
    auto found = vhosts.find(vhost.get().name());
    if (found != vhosts.end()) {
      vhosts.erase(found);
    }
    vhosts.emplace(vhost.get().name(), vhost.get());
    vhosts_added = true;
  }
  return vhosts_added;
}

absl::StatusOr<VhdsSubscriptionPtr> RouteConfigUpdateReceiverImpl::createVhdsSubscription(
    const envoy::config::route::v3::RouteConfiguration& route_config, Init::Manager& init_manager) {
  return VhdsSubscription::createVhdsSubscription(route_config, factory_context_, stat_prefix_,
                                                  *this, init_manager);
}

} // namespace Router
} // namespace Envoy
