#include "source/extensions/clusters/eds/leds.h"

#include "envoy/common/exception.h"
#include "envoy/config/core/v3/config_source.pb.h"

#include "source/common/common/assert.h"
#include "source/common/config/decoded_resource_impl.h"
#include "source/common/config/xds_resource.h"

namespace Envoy {
namespace Upstream {

LedsSubscription::LedsSubscription(
    const envoy::config::endpoint::v3::LedsClusterLocalityConfig& leds_config,
    const std::string& cluster_name,
    Server::Configuration::TransportSocketFactoryContext& factory_context,
    Stats::Scope& cluster_stats_scope, const UpdateCb& callback)
    : Envoy::Config::SubscriptionBase<envoy::config::endpoint::v3::LbEndpoint>(
          factory_context.messageValidationVisitor(), leds_config.leds_collection_name()),
      local_info_(factory_context.serverFactoryContext().localInfo()), cluster_name_(cluster_name),
      stats_scope_(cluster_stats_scope.createScope("leds.")),
      stats_({ALL_LEDS_STATS(POOL_COUNTER(*stats_scope_))}), callback_(callback) {
  const xds::core::v3::ResourceLocator leds_resource_locator = THROW_OR_RETURN_VALUE(
      Config::XdsResourceIdentifier::decodeUrl(leds_config.leds_collection_name()),
      xds::core::v3::ResourceLocator);
  const auto resource_name = getResourceName();
  subscription_ = THROW_OR_RETURN_VALUE(
      factory_context.clusterManager().subscriptionFactory().collectionSubscriptionFromUrl(
          leds_resource_locator, leds_config.leds_config(), resource_name, *stats_scope_, *this,
          resource_decoder_),
      Config::SubscriptionPtr);
  subscription_->start({});
}

absl::Status
LedsSubscription::onConfigUpdate(const std::vector<Config::DecodedResourceRef>& added_resources,
                                 const Protobuf::RepeatedPtrField<std::string>& removed_resources,
                                 const std::string&) {
  // At least one resource must be added or removed.
  if (added_resources.empty() && removed_resources.empty()) {
    ENVOY_LOG(debug, "No added or removed LbEndpoint entries for cluster {} in onConfigUpdate()",
              cluster_name_);
    stats_.update_empty_.inc();
    // If it's the first update, and it has no resources, set the locality as active,
    // and update whoever is waiting for it, to allow the system to initialize.
    if (!initial_update_attempt_complete_) {
      initial_update_attempt_complete_ = true;
      callback_();
    }
    return absl::OkStatus();
  }

  ENVOY_LOG(info, "{}: add {} endpoint(s), remove {} endpoints(s)", cluster_name_,
            added_resources.size(), removed_resources.size());

  // Update the internal host list with the removed hosts.
  for (const auto& removed_resource_name : removed_resources) {
    // Remove the entry from the endpoints list.
    ENVOY_LOG(debug, "Removing endpoint {} using LEDS update.", removed_resource_name);
    endpoints_map_.erase(removed_resource_name);
  }

  // Update the internal host list with the added hosts.
  for (const auto& added_resource : added_resources) {
    const auto& added_resource_name = added_resource.get().name();
    ENVOY_LOG(trace, "Adding/Updating endpoint {} using LEDS update.", added_resource_name);
    envoy::config::endpoint::v3::LbEndpoint lb_endpoint =
        dynamic_cast<const envoy::config::endpoint::v3::LbEndpoint&>(
            added_resource.get().resource());
    endpoints_map_[added_resource_name] = std::move(lb_endpoint);
  }

  // Notify the callbacks that the host list has been modified.
  initial_update_attempt_complete_ = true;
  callback_();
  return absl::OkStatus();
}

void LedsSubscription::onConfigUpdateFailed(Envoy::Config::ConfigUpdateFailureReason reason,
                                            const EnvoyException*) {
  ASSERT(Envoy::Config::ConfigUpdateFailureReason::ConnectionFailure != reason);
  ENVOY_LOG(debug, "LEDS update failed");

  // Similar to EDS, we need to let the system initialize. Set the locality as
  // active, and update whoever is waiting for it.
  initial_update_attempt_complete_ = true;
  callback_();
}

} // namespace Upstream
} // namespace Envoy
