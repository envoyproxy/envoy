#include "common/router/vhds.h"

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>

#include "envoy/api/v2/core/config_source.pb.h"
#include "envoy/api/v2/discovery.pb.h"
#include "envoy/api/v2/route/route.pb.h"

#include "common/common/assert.h"
#include "common/common/fmt.h"
#include "common/config/api_version.h"
#include "common/config/utility.h"
#include "common/protobuf/utility.h"
#include "common/router/config_impl.h"

namespace Envoy {
namespace Router {

// Implements callbacks to handle DeltaDiscovery protocol for VirtualHostDiscoveryService
VhdsSubscription::VhdsSubscription(
    RouteConfigUpdatePtr& config_update_info,
    Server::Configuration::ServerFactoryContext& factory_context, const std::string& stat_prefix,
    std::unordered_set<RouteConfigProvider*>& route_config_providers,
    envoy::api::v2::core::ConfigSource::XdsApiVersion xds_api_version)
    : config_update_info_(config_update_info),
      scope_(factory_context.scope().createScope(stat_prefix + "vhds." +
                                                 config_update_info_->routeConfigName() + ".")),
      stats_({ALL_VHDS_STATS(POOL_COUNTER(*scope_))}),
      init_target_(fmt::format("VhdsConfigSubscription {}", config_update_info_->routeConfigName()),
                   [this]() { subscription_->start({}); }),
      route_config_providers_(route_config_providers), xds_api_version_(xds_api_version) {
  const auto& config_source = config_update_info_->routeConfiguration()
                                  .vhds()
                                  .config_source()
                                  .api_config_source()
                                  .api_type();
  if (config_source != envoy::api::v2::core::ApiConfigSource::DELTA_GRPC) {
    throw EnvoyException("vhds: only 'DELTA_GRPC' is supported as an api_type.");
  }

  subscription_ =
      factory_context.clusterManager().subscriptionFactory().subscriptionFromConfigSource(
          config_update_info_->routeConfiguration().vhds().config_source(), loadTypeUrl(), *scope_,
          *this);
}

void VhdsSubscription::onConfigUpdateFailed(Envoy::Config::ConfigUpdateFailureReason reason,
                                            const EnvoyException*) {
  ASSERT(Envoy::Config::ConfigUpdateFailureReason::ConnectionFailure != reason);
  // We need to allow server startup to continue, even if we have a bad
  // config.
  init_target_.ready();
}

void VhdsSubscription::onConfigUpdate(
    const Protobuf::RepeatedPtrField<envoy::api::v2::Resource>& added_resources,
    const Protobuf::RepeatedPtrField<std::string>& removed_resources,
    const std::string& version_info) {
  if (config_update_info_->onVhdsUpdate(added_resources, removed_resources, version_info)) {
    stats_.config_reload_.inc();
    ENVOY_LOG(debug, "vhds: loading new configuration: config_name={} hash={}",
              config_update_info_->routeConfigName(), config_update_info_->configHash());
    for (auto* provider : route_config_providers_) {
      provider->onConfigUpdate();
    }
  }

  init_target_.ready();
}

std::string VhdsSubscription::loadTypeUrl() {
  switch (xds_api_version_) {
  // automatically set api version as V2
  case envoy::api::v2::core::ConfigSource::AUTO:
  case envoy::api::v2::core::ConfigSource::V2:
    return Grpc::Common::typeUrl(
        API_NO_BOOST(envoy::api::v2::route::VirtualHost().GetDescriptor()->full_name()));
  case envoy::api::v2::core::ConfigSource::V3ALPHA:
    return Grpc::Common::typeUrl(
        API_NO_BOOST(envoy::api::v2::route::VirtualHost().GetDescriptor()->full_name()));
  default:
    throw EnvoyException(fmt::format("type {} is not supported", xds_api_version_));
  }
}
} // namespace Router
} // namespace Envoy
