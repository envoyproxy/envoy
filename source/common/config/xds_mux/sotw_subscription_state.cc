#include "source/common/config/xds_mux/sotw_subscription_state.h"

#include "source/common/config/utility.h"

namespace Envoy {
namespace Config {
namespace XdsMux {

SotwSubscriptionState::SotwSubscriptionState(std::string type_url,
                                             UntypedConfigUpdateCallbacks& callbacks,
                                             Event::Dispatcher& dispatcher,
                                             OpaqueResourceDecoder& resource_decoder)
    : BaseSubscriptionState(std::move(type_url), callbacks, dispatcher),
      resource_decoder_(resource_decoder) {}

SotwSubscriptionState::~SotwSubscriptionState() = default;

void SotwSubscriptionState::updateSubscriptionInterest(
    const absl::flat_hash_set<std::string>& cur_added,
    const absl::flat_hash_set<std::string>& cur_removed) {
  for (const auto& a : cur_added) {
    names_tracked_.insert(a);
  }
  for (const auto& r : cur_removed) {
    names_tracked_.erase(r);
  }
  if (!cur_added.empty() || !cur_removed.empty()) {
    update_pending_ = true;
  }
}

// Not having sent any requests yet counts as an "update pending" since you're supposed to resend
// the entirety of your interest at the start of a stream, even if nothing has changed.
bool SotwSubscriptionState::subscriptionUpdatePending() const {
  return update_pending_ || dynamicContextChanged();
}

void SotwSubscriptionState::markStreamFresh() {
  last_good_version_info_ = absl::nullopt;
  last_good_nonce_ = absl::nullopt;
  update_pending_ = true;
  clearDynamicContextChanged();
}

void SotwSubscriptionState::handleGoodResponse(
    const envoy::service::discovery::v3::DiscoveryResponse& message) {
  std::vector<DecodedResourcePtr> non_heartbeat_resources;
  std::vector<envoy::service::discovery::v3::Resource> resources_with_ttl(
      message.resources().size());

  {
    const auto scoped_update = ttl_.scopedTtlUpdate();
    for (const auto& any : message.resources()) {
      if (!any.Is<envoy::service::discovery::v3::Resource>() &&
          any.type_url() != message.type_url()) {
        throw EnvoyException(fmt::format("type URL {} embedded in an individual Any does not match "
                                         "the message-wide type URL {} in DiscoveryResponse {}",
                                         any.type_url(), message.type_url(),
                                         message.DebugString()));
      }

      auto decoded_resource =
          DecodedResourceImpl::fromResource(resource_decoder_, any, message.version_info());
      setResourceTtl(*decoded_resource);
      if (isHeartbeatResource(*decoded_resource, message.version_info())) {
        continue;
      }
      non_heartbeat_resources.push_back(std::move(decoded_resource));
    }
  }

  // TODO (dmitri-d) to eliminate decoding of resources twice consider expanding the interface to
  // support passing of decoded resources. This would also avoid a resource copy above.
  callbacks().onConfigUpdate(non_heartbeat_resources, message.version_info());
  // Now that we're passed onConfigUpdate() without an exception thrown, we know we're good.
  last_good_version_info_ = message.version_info();
  last_good_nonce_ = message.nonce();
  ENVOY_LOG(debug, "Config update for {} (version {}) accepted with {} resources", typeUrl(),
            message.version_info(), message.resources().size());
}

std::unique_ptr<envoy::service::discovery::v3::DiscoveryRequest>
SotwSubscriptionState::getNextRequestInternal() {
  auto request = std::make_unique<envoy::service::discovery::v3::DiscoveryRequest>();
  request->set_type_url(typeUrl());
  std::copy(names_tracked_.begin(), names_tracked_.end(),
            Protobuf::RepeatedFieldBackInserter(request->mutable_resource_names()));
  if (last_good_version_info_.has_value()) {
    request->set_version_info(last_good_version_info_.value());
  }
  // Default response_nonce to the last known good one. If we are being called by
  // getNextRequestWithAck(), this value will be overwritten.
  if (last_good_nonce_.has_value()) {
    request->set_response_nonce(last_good_nonce_.value());
  }

  update_pending_ = false;
  return request;
}

void SotwSubscriptionState::ttlExpiryCallback(const std::vector<std::string>& expired) {
  Protobuf::RepeatedPtrField<std::string> removed_resources;
  for (const auto& resource : expired) {
    removed_resources.Add(std::string(resource));
  }
  callbacks().onConfigUpdate({}, removed_resources, "");
}

void SotwSubscriptionState::setResourceTtl(const DecodedResourceImpl& decoded_resource) {
  if (decoded_resource.ttl()) {
    ttl_.add(std::chrono::milliseconds(*decoded_resource.ttl()), decoded_resource.name());
  } else {
    ttl_.clear(decoded_resource.name());
  }
}

bool SotwSubscriptionState::isHeartbeatResource(const DecodedResource& resource,
                                                const std::string& version) {
  return !resource.hasResource() && last_good_version_info_.has_value() &&
         version == last_good_version_info_.value();
}

} // namespace XdsMux
} // namespace Config
} // namespace Envoy
