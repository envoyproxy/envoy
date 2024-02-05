#include "source/common/router/route_config_update_receiver_impl.h"

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
                               bool validate_clusters_default) const {
  ASSERT(dynamic_cast<const envoy::config::route::v3::RouteConfiguration*>(&rc));
  return std::make_shared<ConfigImpl>(
      static_cast<const envoy::config::route::v3::RouteConfiguration&>(rc), factory_context,
      validator_, validate_clusters_default);
}

bool RouteConfigUpdateReceiverImpl::onRdsUpdate(const Protobuf::Message& rc,
                                                const std::string& version_info) {
  uint64_t new_hash = base_.getHash(rc);
  if (!base_.checkHash(new_hash)) {
    return false;
  }
  auto new_route_config = std::make_unique<envoy::config::route::v3::RouteConfiguration>();
  new_route_config->CheckTypeAndMergeFrom(rc);
  const uint64_t new_vhds_config_hash =
      new_route_config->has_vhds() ? MessageUtil::hash(new_route_config->vhds()) : 0ul;
  if (new_route_config->has_vhds()) {
    // When using VHDS, stash away RDS vhosts, so that they can be merged with VHDS vhosts in
    // onVhdsUpdate.
    if (rds_virtual_hosts_ == nullptr) {
      rds_virtual_hosts_ = std::make_unique<VirtualHostMap>();
    } else {
      rds_virtual_hosts_->clear();
    }
    for (const auto& vhost : new_route_config->virtual_hosts()) {
      rds_virtual_hosts_->emplace(vhost.name(), vhost);
    }
    if (vhds_virtual_hosts_ != nullptr && !vhds_virtual_hosts_->empty()) {
      // If there are vhosts supplied by VHDS, merge them with RDS vhosts.
      rebuildRouteConfigVirtualHosts(*rds_virtual_hosts_, *vhds_virtual_hosts_, *new_route_config);
    }
  }
  base_.updateConfig(std::move(new_route_config));
  base_.updateHash(new_hash);
  vhds_configuration_changed_ = new_vhds_config_hash != last_vhds_config_hash_;
  last_vhds_config_hash_ = new_vhds_config_hash;

  base_.onUpdateCommon(version_info);
  return true;
}

bool RouteConfigUpdateReceiverImpl::onVhdsUpdate(
    const VirtualHostRefVector& added_vhosts, const std::set<std::string>& added_resource_ids,
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

  auto route_config_after_this_update =
      std::make_unique<envoy::config::route::v3::RouteConfiguration>();
  route_config_after_this_update->CheckTypeAndMergeFrom(base_.protobufConfiguration());
  rebuildRouteConfigVirtualHosts(*rds_virtual_hosts_, *vhosts_after_this_update,
                                 *route_config_after_this_update);

  base_.updateConfig(std::move(route_config_after_this_update));
  // No exception, route_config_after_this_update is valid, can update the state.
  vhds_virtual_hosts_ = std::move(vhosts_after_this_update);
  resource_ids_in_last_update_ = added_resource_ids;
  base_.onUpdateCommon(version_info);

  return removed || updated || !resource_ids_in_last_update_.empty();
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

} // namespace Router
} // namespace Envoy
