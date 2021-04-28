#include "common/upstream/leds.h"

#include "envoy/api/v2/endpoint.pb.h"
#include "envoy/common/exception.h"
#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/config/core/v3/config_source.pb.h"
#include "envoy/service/discovery/v3/discovery.pb.h"

#include "common/common/assert.h"
#include "common/common/utility.h"
#include "common/config/api_version.h"
#include "common/config/decoded_resource_impl.h"
#include "common/config/version_converter.h"
#include "common/config/xds_context_params.h"
#include "common/config/xds_resource.h"

namespace Envoy {
namespace Upstream {

LedsSubscription::LedsSubscription(
    const envoy::config::endpoint::v3::LedsClusterLocalityConfig& leds_config,
    const std::string& cluster_name,
    Server::Configuration::TransportSocketFactoryContextImpl& factory_context,
    /*Stats::Scope& stats_scope,*/ UpdateCb callback)
    : Envoy::Config::SubscriptionBase<envoy::config::endpoint::v3::LbEndpoint>(
          leds_config.leds_config().resource_api_version(),
          factory_context.messageValidationVisitor(), leds_config.leds_collection_name()),
      local_info_(factory_context.localInfo()), cluster_name_(cluster_name),
      stats_scope_(factory_context.scope()), callback_(callback) {
  const xds::core::v3::ResourceLocator leds_resource_locator =
      Config::XdsResourceIdentifier::decodeUrl(leds_config.leds_collection_name());
  const auto resource_name = getResourceName();
  subscription_ =
      factory_context.clusterManager().subscriptionFactory().collectionSubscriptionFromUrl(
          leds_resource_locator, leds_config.leds_config(), resource_name, stats_scope_, *this,
          resource_decoder_);
  subscription_->start({});
}

void LedsSubscription::onConfigUpdate(
    const std::vector<Config::DecodedResourceRef>& added_resources,
    const Protobuf::RepeatedPtrField<std::string>& removed_resources, const std::string&) {
  // At least one resource must be added or removed.
  if (!validateUpdateSize(added_resources.size()) &&
      !validateUpdateSize(removed_resources.size())) {
    ENVOY_LOG(debug, "No added or removed LbEndpoint entries for cluster {} in onConfigUpdate()",
              cluster_name_);
    // If it's the first update, and it has no resources, set the locality as active,
    // and update whoever is waiting for it, to allow the system to initialize.
    if (!active_) {
      active_ = true;
      callback_();
    }
    return;
  }

  ENVOY_LOG(info, "{}: add {} endpoint(s), remove {} endpoints(s)", cluster_name_,
            added_resources.size(), removed_resources.size());

  // Update the internal host list with the removed hosts.
  for (const auto& removed_resource_name : removed_resources) {
    if (!Config::XdsResourceIdentifier::hasXdsTpScheme(removed_resource_name)) {
      ENVOY_LOG(warn,
                "Received non-glob collection resource name for an unsubscribed resource {} in "
                "LEDS update for cluster: {}. Skipping entry.",
                removed_resource_name, cluster_name_);
      continue;
    }

    // Remove the entry from the endpoints list.
    ENVOY_LOG(debug, "Removing endpoint {} using LEDS update.", removed_resource_name);
    auto map_it = endpoint_entry_map_.find(removed_resource_name);
    endpoints_.erase(map_it->second);
    endpoint_entry_map_.erase(map_it);
  }

  // Update the internal host list with the added hosts.
  for (const auto& added_resource : added_resources) {
    const auto& added_resource_name = added_resource.get().name();
    if (!Config::XdsResourceIdentifier::hasXdsTpScheme(added_resource_name)) {
      // Updating the resource contents.
      ENVOY_LOG(trace, "Updating endpoint {} using LEDS update.", added_resource_name);
      envoy::config::endpoint::v3::LbEndpoint lb_endpoint =
          dynamic_cast<const envoy::config::endpoint::v3::LbEndpoint&>(
              added_resource.get().resource());
      *endpoint_entry_map_[added_resource_name] = lb_endpoint;
    } else {
      ENVOY_LOG(trace, "Adding new endpoint {} using LEDS update.", added_resource_name);
      envoy::config::endpoint::v3::LbEndpoint lb_endpoint =
          dynamic_cast<const envoy::config::endpoint::v3::LbEndpoint&>(
              added_resource.get().resource());
      auto new_element_it = endpoints_.emplace(endpoints_.end(), lb_endpoint);
      endpoint_entry_map_.emplace(added_resource_name, new_element_it);
    }
  }

  // Notify the callbacks that the host list has been modified.
  active_ = true;
  callback_();
}

void LedsSubscription::onConfigUpdateFailed(Envoy::Config::ConfigUpdateFailureReason reason,
                                            const EnvoyException* e) {
  ASSERT(Envoy::Config::ConfigUpdateFailureReason::ConnectionFailure != reason);
  ENVOY_LOG(info, "LEDS update failed: {}", e->what());

  // Similar to EDS, we need to let the system initialize. Set the locality as
  // active, and update whoever is waiting for it.
  active_ = true;
  callback_();
}

bool LedsSubscription::validateUpdateSize(int num_resources) {
  if (num_resources == 0) {
    // TODO(adisuissa): update stats.
    // stats_scope_.update_empty_.inc();
    return false;
  }
  return true;
}

} // namespace Upstream
} // namespace Envoy
