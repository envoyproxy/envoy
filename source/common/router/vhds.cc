#include "source/common/router/vhds.h"

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>

#include "envoy/config/core/v3/config_source.pb.h"
#include "envoy/config/subscription.h"
#include "envoy/service/discovery/v3/discovery.pb.h"

#include "source/common/common/assert.h"
#include "source/common/common/fmt.h"
#include "source/common/config/api_version.h"
#include "source/common/config/utility.h"
#include "source/common/protobuf/utility.h"
#include "source/common/router/config_impl.h"

namespace Envoy {
namespace Router {

// Implements callbacks to handle DeltaDiscovery protocol for VirtualHostDiscoveryService
VhdsSubscription::VhdsSubscription(RouteConfigUpdatePtr& config_update_info,
                                   Server::Configuration::ServerFactoryContext& factory_context,
                                   const std::string& stat_prefix,
                                   absl::optional<RouteConfigProvider*>& route_config_provider_opt,
                                   envoy::config::core::v3::ApiVersion resource_api_version)
    : Envoy::Config::SubscriptionBase<envoy::config::route::v3::VirtualHost>(
          resource_api_version,
          factory_context.messageValidationContext().dynamicValidationVisitor(), "name"),
      config_update_info_(config_update_info),
      scope_(factory_context.scope().createScope(stat_prefix + "vhds." +
                                                 config_update_info_->routeConfigName() + ".")),
      stats_({ALL_VHDS_STATS(POOL_COUNTER(*scope_))}),
      init_target_(fmt::format("VhdsConfigSubscription {}", config_update_info_->routeConfigName()),
                   [this]() { subscription_->start({config_update_info_->routeConfigName()}); }),
      route_config_provider_opt_(route_config_provider_opt) {
  const auto& config_source = config_update_info_->protobufConfiguration()
                                  .vhds()
                                  .config_source()
                                  .api_config_source()
                                  .api_type();
  if (config_source != envoy::config::core::v3::ApiConfigSource::DELTA_GRPC) {
    throw EnvoyException("vhds: only 'DELTA_GRPC' is supported as an api_type.");
  }
  const auto resource_name = getResourceName();
  Envoy::Config::SubscriptionOptions options;
  options.use_namespace_matching_ = true;
  subscription_ =
      factory_context.clusterManager().subscriptionFactory().subscriptionFromConfigSource(
          config_update_info_->protobufConfiguration().vhds().config_source(),
          Grpc::Common::typeUrl(resource_name), *scope_, *this, resource_decoder_, options);
}

void VhdsSubscription::updateOnDemand(const std::string& with_route_config_name_prefix) {
  subscription_->requestOnDemandUpdate({with_route_config_name_prefix});
}

void VhdsSubscription::onConfigUpdateFailed(Envoy::Config::ConfigUpdateFailureReason reason,
                                            const EnvoyException*) {
  ASSERT(Envoy::Config::ConfigUpdateFailureReason::ConnectionFailure != reason);
  // We need to allow server startup to continue, even if we have a bad
  // config.
  init_target_.ready();
}

void VhdsSubscription::onConfigUpdate(
    const std::vector<Envoy::Config::DecodedResourceRef>& added_resources,
    const Protobuf::RepeatedPtrField<std::string>& removed_resources,
    const std::string& version_info) {
  RouteConfigUpdateReceiver::VirtualHostRefVector added_vhosts;
  std::set<std::string> added_resource_ids;
  for (const auto& resource : added_resources) {
    added_resource_ids.emplace(resource.get().name());
    std::copy(resource.get().aliases().begin(), resource.get().aliases().end(),
              std::inserter(added_resource_ids, added_resource_ids.end()));
    // the management server returns empty resources (they contain no virtual hosts in this case)
    // for aliases that it couldn't resolve.
    if (!resource.get().hasResource()) {
      continue;
    }
    added_vhosts.emplace_back(
        dynamic_cast<const envoy::config::route::v3::VirtualHost&>(resource.get().resource()));
  }
  if (config_update_info_->onVhdsUpdate(added_vhosts, added_resource_ids, removed_resources,
                                        version_info)) {
    stats_.config_reload_.inc();
    ENVOY_LOG(debug, "vhds: loading new configuration: config_name={} hash={}",
              config_update_info_->routeConfigName(), config_update_info_->configHash());
    if (route_config_provider_opt_.has_value()) {
      route_config_provider_opt_.value()->onConfigUpdate();
    }
  }

  init_target_.ready();
}

} // namespace Router
} // namespace Envoy
