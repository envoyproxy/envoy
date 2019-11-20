#include "common/router/vhds.h"

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>

#include "envoy/api/v2/rds.pb.validate.h"
#include "envoy/api/v2/route/route.pb.validate.h"

#include "common/common/assert.h"
#include "common/common/fmt.h"
#include "common/config/utility.h"
#include "common/protobuf/utility.h"
#include "common/router/config_impl.h"

namespace Envoy {
namespace Router {

// Implements callbacks to handle DeltaDiscovery protocol for VirtualHostDiscoveryService
VhdsSubscription::VhdsSubscription(RouteConfigUpdatePtr& config_update_info,
                                   Server::Configuration::ServerFactoryContext& factory_context,
                                   const std::string& stat_prefix,
                                   std::unordered_set<RouteConfigProvider*>& route_config_providers)
    : config_update_info_(config_update_info),
      scope_(factory_context.scope().createScope(stat_prefix + "vhds." +
                                                 config_update_info_->routeConfigName() + ".")),
      stats_({ALL_VHDS_STATS(POOL_COUNTER(*scope_))}),
      init_target_(fmt::format("VhdsConfigSubscription {}", config_update_info_->routeConfigName()),
                   [this]() { subscription_->start({}); }),
      route_config_providers_(route_config_providers) {
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
          config_update_info_->routeConfiguration().vhds().config_source(),
          Grpc::Common::typeUrl(envoy::api::v2::route::VirtualHost().GetDescriptor()->full_name()),
          *scope_, *this);
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

} // namespace Router
} // namespace Envoy
