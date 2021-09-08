#include "source/common/config/xds_mux/delta_subscription_state.h"

#include "envoy/event/dispatcher.h"
#include "envoy/service/discovery/v3/discovery.pb.h"

#include "source/common/common/hash.h"
#include "source/common/config/utility.h"
#include "source/common/runtime/runtime_features.h"

namespace Envoy {
namespace Config {
namespace XdsMux {

DeltaSubscriptionState::DeltaSubscriptionState(std::string type_url,
                                               UntypedConfigUpdateCallbacks& watch_map,
                                               Event::Dispatcher& dispatcher, const bool wildcard)
    : BaseSubscriptionState(std::move(type_url), watch_map, dispatcher),
      // TODO(snowp): Hard coding VHDS here is temporary until we can move it away from relying on
      // empty resources as updates.
      supports_heartbeats_(type_url_ != "envoy.config.route.v3.VirtualHost"), wildcard_(wildcard) {}

DeltaSubscriptionState::~DeltaSubscriptionState() = default;

void DeltaSubscriptionState::updateSubscriptionInterest(
    const absl::flat_hash_set<std::string>& cur_added,
    const absl::flat_hash_set<std::string>& cur_removed) {
  for (const auto& a : cur_added) {
    resource_state_[a] = ResourceState::waitingForServer();
    // If interest in a resource is removed-then-added (all before a discovery request
    // can be sent), we must treat it as a "new" addition: our user may have forgotten its
    // copy of the resource after instructing us to remove it, and need to be reminded of it.
    names_removed_.erase(a);
    names_added_.insert(a);
  }
  for (const auto& r : cur_removed) {
    resource_state_.erase(r);
    // Ideally, when interest in a resource is added-then-removed in between requests,
    // we would avoid putting a superfluous "unsubscribe [resource that was never subscribed]"
    // in the request. However, the removed-then-added case *does* need to go in the request,
    // and due to how we accomplish that, it's difficult to distinguish remove-add-remove from
    // add-remove (because "remove-add" has to be treated as equivalent to just "add").
    names_added_.erase(r);
    names_removed_.insert(r);
  }
}

// Not having sent any requests yet counts as an "update pending" since you're supposed to resend
// the entirety of your interest at the start of a stream, even if nothing has changed.
bool DeltaSubscriptionState::subscriptionUpdatePending() const {
  return !names_added_.empty() || !names_removed_.empty() ||
         !any_request_sent_yet_in_current_stream_ || dynamicContextChanged();
}

bool DeltaSubscriptionState::isHeartbeatResource(
    const envoy::service::discovery::v3::Resource& resource) const {
  if (!supports_heartbeats_ &&
      !Runtime::runtimeFeatureEnabled("envoy.reloadable_features.vhds_heartbeats")) {
    return false;
  }
  const auto itr = resource_state_.find(resource.name());
  if (itr == resource_state_.end()) {
    return false;
  }

  return !resource.has_resource() && !itr->second.isWaitingForServer() &&
         resource.version() == itr->second.version();
}

void DeltaSubscriptionState::handleGoodResponse(
    const envoy::service::discovery::v3::DeltaDiscoveryResponse& message) {
  absl::flat_hash_set<std::string> names_added_removed;
  Protobuf::RepeatedPtrField<envoy::service::discovery::v3::Resource> non_heartbeat_resources;
  for (const auto& resource : message.resources()) {
    if (!names_added_removed.insert(resource.name()).second) {
      throw EnvoyException(
          fmt::format("duplicate name {} found among added/updated resources", resource.name()));
    }
    if (isHeartbeatResource(resource)) {
      continue;
    }
    // TODO (dmitri-d) consider changing onConfigUpdate callback interface to avoid copying of
    // resources
    non_heartbeat_resources.Add()->CopyFrom(resource);
    // DeltaDiscoveryResponses for unresolved aliases don't contain an actual resource
    if (!resource.has_resource() && resource.aliases_size() > 0) {
      continue;
    }
    if (message.type_url() != resource.resource().type_url()) {
      throw EnvoyException(fmt::format("type URL {} embedded in an individual Any does not match "
                                       "the message-wide type URL {} in DeltaDiscoveryResponse {}",
                                       resource.resource().type_url(), message.type_url(),
                                       message.DebugString()));
    }
  }
  for (const auto& name : message.removed_resources()) {
    if (!names_added_removed.insert(name).second) {
      throw EnvoyException(
          fmt::format("duplicate name {} found in the union of added+removed resources", name));
    }
  }

  {
    const auto scoped_update = ttl_.scopedTtlUpdate();
    for (const auto& resource : message.resources()) {
      addResourceState(resource);
    }
  }

  callbacks().onConfigUpdate(non_heartbeat_resources, message.removed_resources(),
                             message.system_version_info());

  // If a resource is gone, there is no longer a meaningful version for it that makes sense to
  // provide to the server upon stream reconnect: either it will continue to not exist, in which
  // case saying nothing is fine, or the server will bring back something new, which we should
  // receive regardless (which is the logic that not specifying a version will get you).
  //
  // So, leave the version map entry present but blank. It will be left out of
  // initial_resource_versions messages, but will remind us to explicitly tell the server "I'm
  // cancelling my subscription" when we lose interest.
  for (const auto& resource_name : message.removed_resources()) {
    if (resource_state_.find(resource_name) != resource_state_.end()) {
      resource_state_[resource_name] = ResourceState::waitingForServer();
    }
  }
  ENVOY_LOG(debug, "Delta config for {} accepted with {} resources added, {} removed", typeUrl(),
            message.resources().size(), message.removed_resources().size());
}

std::unique_ptr<envoy::service::discovery::v3::DeltaDiscoveryRequest>
DeltaSubscriptionState::getNextRequestInternal() {
  auto request = std::make_unique<envoy::service::discovery::v3::DeltaDiscoveryRequest>();
  request->set_type_url(typeUrl());
  if (!any_request_sent_yet_in_current_stream_) {
    any_request_sent_yet_in_current_stream_ = true;
    // initial_resource_versions "must be populated for first request in a stream".
    // Also, since this might be a new server, we must explicitly state *all* of our subscription
    // interest.
    for (auto const& [resource_name, resource_state] : resource_state_) {
      // Populate initial_resource_versions with the resource versions we currently have.
      // Resources we are interested in, but are still waiting to get any version of from the
      // server, do not belong in initial_resource_versions. (But do belong in new subscriptions!)
      if (!resource_state.isWaitingForServer()) {
        (*request->mutable_initial_resource_versions())[resource_name] = resource_state.version();
      }
      // As mentioned above, fill resource_names_subscribe with everything, including names we
      // have yet to receive any resource for unless this is a wildcard subscription, for which
      // the first request on a stream must be without any resource names.
      if (!wildcard_) {
        names_added_.insert(resource_name);
      }
    }
    // Wildcard subscription initial requests must have no resource_names_subscribe.
    if (wildcard_) {
      names_added_.clear();
    }
    names_removed_.clear();
  }

  std::copy(names_added_.begin(), names_added_.end(),
            Protobuf::RepeatedFieldBackInserter(request->mutable_resource_names_subscribe()));
  std::copy(names_removed_.begin(), names_removed_.end(),
            Protobuf::RepeatedFieldBackInserter(request->mutable_resource_names_unsubscribe()));
  names_added_.clear();
  names_removed_.clear();

  return request;
}

void DeltaSubscriptionState::addResourceState(
    const envoy::service::discovery::v3::Resource& resource) {
  setResourceTtl(resource);
  resource_state_[resource.name()] = ResourceState(resource.version());
}

void DeltaSubscriptionState::setResourceTtl(
    const envoy::service::discovery::v3::Resource& resource) {
  if (resource.has_ttl()) {
    ttl_.add(std::chrono::milliseconds(DurationUtil::durationToMilliseconds(resource.ttl())),
             resource.name());
  } else {
    ttl_.clear(resource.name());
  }
}

void DeltaSubscriptionState::ttlExpiryCallback(const std::vector<std::string>& expired) {
  Protobuf::RepeatedPtrField<std::string> removed_resources;
  for (const auto& resource : expired) {
    resource_state_[resource] = ResourceState::waitingForServer();
    removed_resources.Add(std::string(resource));
  }
  callbacks().onConfigUpdate({}, removed_resources, "");
}

} // namespace XdsMux
} // namespace Config
} // namespace Envoy
