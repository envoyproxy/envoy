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
    const envoy::config::route::v3::RouteConfiguration& route_config,
    Server::Configuration::ServerFactoryContext& factory_context, const std::string& stat_prefix,
    VhdsConfigUpdateReceiver& receiver, Init::Manager& init_manager) {
  const auto& vhds_config_source = route_config.vhds().config_source();
  // VHDS only supports Delta xDS. This can be specified either explicitly via DELTA_GRPC
  // or implicitly by using ADS when the parent ADS stream is in Delta mode.
  const bool is_ads = vhds_config_source.config_source_specifier_case() ==
                      envoy::config::core::v3::ConfigSource::ConfigSourceSpecifierCase::kAds;
  const bool is_delta_grpc = vhds_config_source.has_api_config_source() &&
                             vhds_config_source.api_config_source().api_type() ==
                                 envoy::config::core::v3::ApiConfigSource::DELTA_GRPC;

  if (!is_ads && !is_delta_grpc) {
    return absl::InvalidArgumentError(
        "vhds: only 'DELTA_GRPC' or 'ADS' (which uses Delta xDS) is supported as a config source.");
  }

  // If using ADS, verify the parent ADS stream is in Delta mode
  if (is_ads) {
    const auto& bootstrap = factory_context.bootstrap();
    if (!bootstrap.has_dynamic_resources() || !bootstrap.dynamic_resources().has_ads_config()) {
      return absl::InvalidArgumentError(
          "vhds: ADS config source specified but no ADS configured in bootstrap.");
    }
    const auto& ads_config = bootstrap.dynamic_resources().ads_config();
    if (ads_config.api_type() != envoy::config::core::v3::ApiConfigSource::DELTA_GRPC) {
      return absl::InvalidArgumentError(
          "vhds: ADS must use DELTA_GRPC api_type when used as VHDS config source.");
    }
  }

  auto status = absl::OkStatus();
  auto ret = std::unique_ptr<VhdsSubscription>(new VhdsSubscription(
      route_config, factory_context, stat_prefix, receiver, init_manager, status));
  RETURN_IF_ERROR(status);
  return ret;
}

// Implements callbacks to handle DeltaDiscovery protocol for VirtualHostDiscoveryService
VhdsSubscription::VhdsSubscription(const envoy::config::route::v3::RouteConfiguration& route_config,
                                   Server::Configuration::ServerFactoryContext& factory_context,
                                   const std::string& stat_prefix,
                                   VhdsConfigUpdateReceiver& receiver, Init::Manager& init_manager,
                                   absl::Status& status)
    : receiver_(receiver), route_config_name_(route_config.name()),
      scope_(factory_context.scope().createScope(stat_prefix + "vhds." + route_config_name_ + ".")),
      stats_({ALL_VHDS_STATS(POOL_COUNTER(*scope_))}),
      init_target_(fmt::format("VhdsConfigSubscription {}", route_config_name_),
                   [this]() { subscription_->start({route_config_name_}); }),
      resource_type_helper_(factory_context.messageValidationContext().dynamicValidationVisitor(),
                            "name") {
  const auto resource_name = resource_type_helper_.getResourceName();
  Envoy::Config::SubscriptionOptions options;
  options.use_namespace_matching_ = true;
  absl::StatusOr<Envoy::Config::SubscriptionPtr> status_or =
      factory_context.clusterManager().subscriptionFactory().subscriptionFromConfigSource(
          route_config.vhds().config_source(), Grpc::Common::typeUrl(resource_name), *scope_, *this,
          resource_type_helper_.resourceDecoder(), options);
  SET_AND_RETURN_IF_NOT_OK(status_or.status(), status);
  subscription_ = std::move(status_or.value());
  // Registered last, so that the target's callback never runs before subscription_ is set. That
  // can't happen with the per-update init manager, which is always Uninitialized here, but this
  // keeps it true regardless of which init manager is handed in.
  init_manager.add(init_target_);
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
  VhdsConfigUpdateReceiver::VirtualHostRefVector added_vhosts;
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
        Envoy::Protobuf::DynamicCastMessage<envoy::config::route::v3::VirtualHost>(
            resource.get().resource()));
  }
  // The receiver builds the new route configuration, warms it up and publishes it to its observer.
  // This subscription doesn't publish anything itself.
  if (receiver_.onVhdsUpdate(added_vhosts, std::move(added_resource_ids), removed_resources,
                             version_info)) {
    stats_.config_reload_.inc();
    ENVOY_LOG(debug, "vhds: loading new configuration: config_name={} version={}",
              route_config_name_, version_info);
  }

  init_target_.ready();
  return absl::OkStatus();
}

} // namespace Router
} // namespace Envoy
