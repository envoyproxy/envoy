#include "source/common/config/xds_mux/sotw_subscription_state.h"

#include "source/common/config/utility.h"
#include "source/common/config/xds_source_id.h"

namespace Envoy {
namespace Config {
namespace XdsMux {

SotwSubscriptionState::SotwSubscriptionState(std::string type_url,
                                             UntypedConfigUpdateCallbacks& callbacks,
                                             Event::Dispatcher& dispatcher,
                                             OpaqueResourceDecoderSharedPtr resource_decoder,
                                             XdsResourcesDelegateOptRef xds_resources_delegate,
                                             const std::string& target_xds_authority)
    : BaseSubscriptionState(std::move(type_url), callbacks, dispatcher, xds_resources_delegate,
                            target_xds_authority),
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
  last_good_nonce_ = absl::nullopt;
  update_pending_ = true;
  clearDynamicContextChanged();
}

void SotwSubscriptionState::handleGoodResponse(
    const envoy::service::discovery::v3::DiscoveryResponse& message) {
  std::vector<DecodedResourcePtr> non_heartbeat_resources;

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
          DecodedResourceImpl::fromResource(*resource_decoder_, any, message.version_info());
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

  // Send the resources to the xDS delegate, if configured.
  if (xds_resources_delegate_.has_value()) {
    XdsConfigSourceId source_id{target_xds_authority_, message.type_url()};
    std::vector<DecodedResourceRef> resource_refs;
    resource_refs.reserve(non_heartbeat_resources.size());
    for (const DecodedResourcePtr& r : non_heartbeat_resources) {
      resource_refs.emplace_back(*r);
    }
    xds_resources_delegate_->onConfigUpdated(source_id, resource_refs);
  }

  ENVOY_LOG(debug, "Config update for {} (version {}) accepted with {} resources", typeUrl(),
            message.version_info(), message.resources().size());
}

void SotwSubscriptionState::handleEstablishmentFailure() {
  BaseSubscriptionState::handleEstablishmentFailure();

  if (previously_fetched_data_ || !xds_resources_delegate_.has_value()) {
    return;
  }

  const XdsConfigSourceId source_id{target_xds_authority_, type_url_};
  TRY_ASSERT_MAIN_THREAD {
    std::vector<envoy::service::discovery::v3::Resource> resources =
        xds_resources_delegate_->getResources(source_id, names_tracked_);

    std::vector<DecodedResourcePtr> decoded_resources;
    const auto scoped_update = ttl_.scopedTtlUpdate();
    std::string version_info;
    size_t unaccounted = names_tracked_.size();
    if (names_tracked_.size() == 1 && names_tracked_.contains(Envoy::Config::Wildcard)) {
      // For wildcard requests, there are no expectations for the number of resources returned.
      unaccounted = 0;
    }

    for (const auto& resource : resources) {
      if (version_info.empty()) {
        version_info = resource.version();
      } else {
        ASSERT(version_info == resource.version());
      }

      TRY_ASSERT_MAIN_THREAD {
        auto decoded_resource = DecodedResourceImpl::fromResource(*resource_decoder_, resource);
        setResourceTtl(*decoded_resource);
        if (unaccounted > 0) {
          --unaccounted;
        }
        decoded_resources.emplace_back(std::move(decoded_resource));
      }
      END_TRY
      catch (const EnvoyException& e) {
        xds_resources_delegate_->onResourceLoadFailed(source_id, resource.name(), e);
      }
    }

    callbacks().onConfigUpdate(decoded_resources, version_info);
    previously_fetched_data_ = true;
    if (unaccounted == 0 && !version_info.empty()) {
      // All the requested resources were found and validated from the xDS delegate, so set the last
      // known good version.
      last_good_version_info_ = version_info;
    }
  }
  END_TRY
  catch (const EnvoyException& e) {
    // TODO(abeyad): do something more than just logging the error?
    ENVOY_LOG(warn, "xDS delegate failed onEstablishmentFailure() for {}: {}", source_id.toKey(),
              e.what());
  }
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
