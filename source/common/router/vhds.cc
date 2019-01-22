#include "common/router/vhds.h"

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>

#include "envoy/api/v2/rds.pb.validate.h"
#include "envoy/api/v2/route/route.pb.validate.h"

#include "common/common/assert.h"
#include "common/common/fmt.h"
#include "common/config/rds_json.h"
#include "common/config/utility.h"
#include "common/protobuf/utility.h"
#include "common/router/config_impl.h"

namespace Envoy {
namespace Router {

VhdsSubscription::VhdsSubscription(const envoy::api::v2::RouteConfiguration& route_configuration,
                                   Server::Configuration::FactoryContext& factory_context,
                                   const std::string& stat_prefix,
                                   std::unordered_set<RouteConfigProvider*>& route_config_providers,
                                   SubscriptionFactoryFunction factory_function)
    : route_config_proto_(route_configuration), route_config_name_(route_configuration.name()),
      init_target_(fmt::format("VhdsConfigSubscription {}", route_config_name_),
                   [this]() { subscription_->start({}, *this); }),
      scope_(factory_context.scope().createScope(stat_prefix + "vhds." + route_config_name_ + ".")),
      stats_({ALL_VHDS_STATS(POOL_COUNTER(*scope_))}), time_source_(factory_context.timeSource()),
      last_updated_(factory_context.timeSource().systemTime()),
      route_config_providers_(route_config_providers) {
  Envoy::Config::Utility::checkLocalInfo("vhds", factory_context.localInfo());
  const auto& config_source =
      route_configuration.vhds().config_source().api_config_source().api_type();
  if (config_source != envoy::api::v2::core::ApiConfigSource::DELTA_GRPC) {
    throw EnvoyException("vhds: only 'DELTA_GRPC' is supported as an api_type.");
  }

  initializeVhosts(route_config_proto_);
  subscription_ = factory_function(
      route_configuration.vhds().config_source(), factory_context.localInfo(),
      factory_context.dispatcher(), factory_context.clusterManager(), factory_context.random(),
      *scope_, "none", "envoy.api.v2.VirtualHostDiscoveryService.DeltaVirtualHosts",
      Grpc::Common::typeUrl(envoy::api::v2::route::VirtualHost().GetDescriptor()->full_name()),
      factory_context.api());
}

void VhdsSubscription::ondemandUpdate(const std::vector<std::string>& aliases) {
  subscription_->updateResourcesViaAliases(aliases);
}

void VhdsSubscription::onConfigUpdateFailed(const EnvoyException*) {
  // We need to allow server startup to continue, even if we have a bad
  // config.
  init_target_.ready();
}

void VhdsSubscription::onConfigUpdate(
    const Protobuf::RepeatedPtrField<envoy::api::v2::Resource>& added_resources,
    const Protobuf::RepeatedPtrField<std::string>& removed_resources,
    const std::string& version_info) {
  last_updated_ = time_source_.systemTime();
  removeVhosts(virtual_hosts_, removed_resources);
  updateVhosts(virtual_hosts_, added_resources);
  rebuildRouteConfig(virtual_hosts_, route_config_proto_);

  const uint64_t new_hash = MessageUtil::hash(route_config_proto_);
  if (!config_info_ || new_hash != config_info_.value().last_config_hash_) {
    config_info_ = {new_hash, version_info};
    stats_.config_reload_.inc();
    ENVOY_LOG(debug, "vhds: loading new configuration: config_name={} hash={}", route_config_name_,
              new_hash);
    for (auto* provider : route_config_providers_) {
      provider->onConfigUpdate();
    }
  }

  init_target_.ready();
}

void VhdsSubscription::initializeVhosts(
    const envoy::api::v2::RouteConfiguration& route_configuration) {
  for (const auto& vhost : route_configuration.virtual_hosts()) {
    virtual_hosts_.emplace(vhost.name(), vhost);
  }
}

void VhdsSubscription::removeVhosts(
    std::unordered_map<std::string, envoy::api::v2::route::VirtualHost>& vhosts,
    const Protobuf::RepeatedPtrField<std::string>& removed_vhost_names) {
  for (const auto& vhost_name : removed_vhost_names) {
    vhosts.erase(vhost_name);
  }
}

void VhdsSubscription::updateVhosts(
    std::unordered_map<std::string, envoy::api::v2::route::VirtualHost>& vhosts,
    const Protobuf::RepeatedPtrField<envoy::api::v2::Resource>& added_resources) {
  // TODO (dmitri-d) validate added_resources?
  for (const auto& resource : added_resources) {
    envoy::api::v2::route::VirtualHost vhost =
        MessageUtil::anyConvert<envoy::api::v2::route::VirtualHost>(resource.resource());
    auto found = vhosts.find(vhost.name());
    if (found != vhosts.end()) {
      vhosts.erase(found);
    }
    vhosts.emplace(vhost.name(), vhost);
  }
}

void VhdsSubscription::rebuildRouteConfig(
    const std::unordered_map<std::string, envoy::api::v2::route::VirtualHost>& vhosts,
    envoy::api::v2::RouteConfiguration& route_config) {

  route_config.clear_virtual_hosts();
  for (const auto& vhost : vhosts) {
    route_config.mutable_virtual_hosts()->Add()->CopyFrom(vhost.second);
  }
}

} // namespace Router
} // namespace Envoy
