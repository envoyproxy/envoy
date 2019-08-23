#include "common/router/route_config_update_receiver_impl.h"

#include <string>

#include "envoy/api/v2/route/route.pb.validate.h"

#include "common/common/assert.h"
#include "common/common/fmt.h"
#include "common/protobuf/utility.h"
#include "common/router/config_impl.h"

namespace Envoy {
namespace Router {

bool RouteConfigUpdateReceiverImpl::onRdsUpdate(const envoy::api::v2::RouteConfiguration& rc,
                                                const std::string& version_info) {
  const uint64_t new_hash = MessageUtil::hash(rc);
  if (new_hash == last_config_hash_) {
    return false;
  }

  route_config_proto_ = rc;
  last_config_hash_ = new_hash;
  last_config_version_ = version_info;
  last_updated_ = time_source_.systemTime();
  initializeVhosts(route_config_proto_);
  config_info_.emplace(RouteConfigProvider::ConfigInfo{route_config_proto_, last_config_version_});
  return true;
}

bool RouteConfigUpdateReceiverImpl::onVhdsUpdate(
    const Protobuf::RepeatedPtrField<envoy::api::v2::Resource>& added_resources,
    const Protobuf::RepeatedPtrField<std::string>& removed_resources,
    const std::string& version_info) {
  removeVhosts(virtual_hosts_, removed_resources);
  updateVhosts(virtual_hosts_, added_resources);
  rebuildRouteConfig(virtual_hosts_, route_config_proto_);

  return onRdsUpdate(route_config_proto_, version_info);
}

void RouteConfigUpdateReceiverImpl::initializeVhosts(
    const envoy::api::v2::RouteConfiguration& route_configuration) {
  virtual_hosts_.clear();
  for (const auto& vhost : route_configuration.virtual_hosts()) {
    virtual_hosts_.emplace(vhost.name(), vhost);
  }
}

void RouteConfigUpdateReceiverImpl::removeVhosts(
    std::unordered_map<std::string, envoy::api::v2::route::VirtualHost>& vhosts,
    const Protobuf::RepeatedPtrField<std::string>& removed_vhost_names) {
  for (const auto& vhost_name : removed_vhost_names) {
    vhosts.erase(vhost_name);
  }
}

void RouteConfigUpdateReceiverImpl::updateVhosts(
    std::unordered_map<std::string, envoy::api::v2::route::VirtualHost>& vhosts,
    const Protobuf::RepeatedPtrField<envoy::api::v2::Resource>& added_resources) {
  for (const auto& resource : added_resources) {
    envoy::api::v2::route::VirtualHost vhost =
        MessageUtil::anyConvert<envoy::api::v2::route::VirtualHost>(resource.resource());
    MessageUtil::validate(vhost, validation_visitor_);
    auto found = vhosts.find(vhost.name());
    if (found != vhosts.end()) {
      vhosts.erase(found);
    }
    vhosts.emplace(vhost.name(), vhost);
  }
}

void RouteConfigUpdateReceiverImpl::rebuildRouteConfig(
    const std::unordered_map<std::string, envoy::api::v2::route::VirtualHost>& vhosts,
    envoy::api::v2::RouteConfiguration& route_config) {
  route_config.clear_virtual_hosts();
  for (const auto& vhost : vhosts) {
    route_config.mutable_virtual_hosts()->Add()->CopyFrom(vhost.second);
  }
}

} // namespace Router
} // namespace Envoy
