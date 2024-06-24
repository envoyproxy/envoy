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
#include "source/common/grpc/common.h"
#include "source/common/protobuf/utility.h"
#include "source/common/router/config_impl.h"

namespace Envoy {
namespace Router {

absl::StatusOr<VhdsSubscriptionPtr> VhdsSubscription::createVhdsSubscription(
    RouteConfigUpdatePtr& config_update_info,
    Server::Configuration::ServerFactoryContext& factory_context, const std::string& stat_prefix,
    Rds::RouteConfigProvider* route_config_provider) {
  const auto& config_source = config_update_info->protobufConfigurationCast()
                                  .vhds()
                                  .config_source()
                                  .api_config_source()
                                  .api_type();
  if (config_source != envoy::config::core::v3::ApiConfigSource::DELTA_GRPC) {
    return absl::InvalidArgumentError("vhds: only 'DELTA_GRPC' is supported as an api_type.");
  }

  return std::unique_ptr<VhdsSubscription>(new VhdsSubscription(
      config_update_info, factory_context, stat_prefix, route_config_provider));
}

// Implements callbacks to handle DeltaDiscovery protocol for VirtualHostDiscoveryService
VhdsSubscription::VhdsSubscription(RouteConfigUpdatePtr& config_update_info,
                                   Server::Configuration::ServerFactoryContext& factory_context,
                                   const std::string& stat_prefix,
                                   Rds::RouteConfigProvider* route_config_provider)
    : Envoy::Config::SubscriptionBase<envoy::config::route::v3::VirtualHost>(
          factory_context.messageValidationContext().dynamicValidationVisitor(), "name"),
      config_update_info_(config_update_info),
      scope_(factory_context.scope().createScope(
          stat_prefix + "vhds." + config_update_info_->protobufConfigurationCast().name() + ".")),
      stats_({ALL_VHDS_STATS(POOL_COUNTER(*scope_))}),
      init_target_(fmt::format("VhdsConfigSubscription {}",
                               config_update_info_->protobufConfigurationCast().name()),
                   [this]() {
                     subscription_->start(
                         {config_update_info_->protobufConfigurationCast().name()});
                   }),
      route_config_provider_(route_config_provider) {
  const auto resource_name = getResourceName();
  Envoy::Config::SubscriptionOptions options;
  options.use_namespace_matching_ = true;
  subscription_ = THROW_OR_RETURN_VALUE(
      factory_context.clusterManager().subscriptionFactory().subscriptionFromConfigSource(
          config_update_info_->protobufConfigurationCast().vhds().config_source(),
          Grpc::Common::typeUrl(resource_name), *scope_, *this, resource_decoder_, options),
      Envoy::Config::SubscriptionPtr);
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

absl::Status VhdsSubscription::onConfigUpdate(
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
              config_update_info_->protobufConfigurationCast().name(),
              config_update_info_->configHash());
    if (route_config_provider_ != nullptr) {
      RETURN_IF_NOT_OK(route_config_provider_->onConfigUpdate());
    }
  }

  init_target_.ready();
  return absl::OkStatus();
}

} // namespace Router
} // namespace Envoy
