#include "source/common/router/route_config_update_receiver_impl.h"

#include <string>

#include "envoy/config/route/v3/route.pb.h"
#include "envoy/service/discovery/v3/discovery.pb.h"

#include "source/common/common/assert.h"
#include "source/common/common/fmt.h"
#include "source/common/common/thread.h"
#include "source/common/protobuf/utility.h"
#include "source/common/router/config_impl.h"

namespace Envoy {
namespace Router {

bool RouteConfigUpdateReceiverImpl::onRdsUpdate(
    const envoy::config::route::v3::RouteConfiguration& rc, const std::string& version_info) {
  const uint64_t new_hash = MessageUtil::hash(rc);
  if (new_hash == last_config_hash_) {
    return false;
  }
  route_config_proto_ = std::make_unique<envoy::config::route::v3::RouteConfiguration>(rc);
  last_config_hash_ = new_hash;
  const uint64_t new_vhds_config_hash = rc.has_vhds() ? MessageUtil::hash(rc.vhds()) : 0ul;
  vhds_configuration_changed_ = new_vhds_config_hash != last_vhds_config_hash_;
  last_vhds_config_hash_ = new_vhds_config_hash;
  initializeRdsVhosts(*route_config_proto_);

  rebuildRouteConfig(rds_virtual_hosts_, *vhds_virtual_hosts_, *route_config_proto_);
  config_ = std::make_shared<ConfigImpl>(
      *route_config_proto_, optional_http_filters_, factory_context_,
      factory_context_.messageValidationContext().dynamicValidationVisitor(), false);

  onUpdateCommon(version_info);
  return true;
}

bool RouteConfigUpdateReceiverImpl::onVhdsUpdate(
    const VirtualHostRefVector& added_vhosts, const std::set<std::string>& added_resource_ids,
    const Protobuf::RepeatedPtrField<std::string>& removed_resources,
    const std::string& version_info) {

  auto vhosts_after_this_update =
      std::make_unique<std::map<std::string, envoy::config::route::v3::VirtualHost>>(
          *vhds_virtual_hosts_);
  const bool removed = removeVhosts(*vhosts_after_this_update, removed_resources);
  const bool updated = updateVhosts(*vhosts_after_this_update, added_vhosts);

  auto route_config_after_this_update =
      std::make_unique<envoy::config::route::v3::RouteConfiguration>();
  route_config_after_this_update->CopyFrom(*route_config_proto_);
  rebuildRouteConfig(rds_virtual_hosts_, *vhosts_after_this_update,
                     *route_config_after_this_update);

  auto new_config = std::make_shared<ConfigImpl>(
      *route_config_after_this_update, optional_http_filters_, factory_context_,
      factory_context_.messageValidationContext().dynamicValidationVisitor(), false);

  // No exception, route_config_after_this_update is valid, can update the state.
  vhds_virtual_hosts_ = std::move(vhosts_after_this_update);
  route_config_proto_ = std::move(route_config_after_this_update);
  config_ = new_config;
  resource_ids_in_last_update_ = added_resource_ids;
  onUpdateCommon(version_info);

  return removed || updated || !resource_ids_in_last_update_.empty();
}

void RouteConfigUpdateReceiverImpl::onUpdateCommon(const std::string& version_info) {
  last_config_version_ = version_info;
  last_updated_ = time_source_.systemTime();
  config_info_.emplace(RouteConfigProvider::ConfigInfo{*route_config_proto_, last_config_version_});
}

void RouteConfigUpdateReceiverImpl::initializeRdsVhosts(
    const envoy::config::route::v3::RouteConfiguration& route_configuration) {
  rds_virtual_hosts_.clear();
  for (const auto& vhost : route_configuration.virtual_hosts()) {
    rds_virtual_hosts_.emplace(vhost.name(), vhost);
  }
}

bool RouteConfigUpdateReceiverImpl::removeVhosts(
    std::map<std::string, envoy::config::route::v3::VirtualHost>& vhosts,
    const Protobuf::RepeatedPtrField<std::string>& removed_vhost_names) {
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

bool RouteConfigUpdateReceiverImpl::updateVhosts(
    std::map<std::string, envoy::config::route::v3::VirtualHost>& vhosts,
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

void RouteConfigUpdateReceiverImpl::rebuildRouteConfig(
    const std::map<std::string, envoy::config::route::v3::VirtualHost>& rds_vhosts,
    const std::map<std::string, envoy::config::route::v3::VirtualHost>& vhds_vhosts,
    envoy::config::route::v3::RouteConfiguration& route_config) {
  route_config.clear_virtual_hosts();
  for (const auto& vhost : rds_vhosts) {
    route_config.mutable_virtual_hosts()->Add()->CopyFrom(vhost.second);
  }
  for (const auto& vhost : vhds_vhosts) {
    route_config.mutable_virtual_hosts()->Add()->CopyFrom(vhost.second);
  }
}

} // namespace Router
} // namespace Envoy
