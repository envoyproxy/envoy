#include "source/common/config/delta_subscription_state.h"

#include "envoy/event/dispatcher.h"
#include "envoy/service/discovery/v3/discovery.pb.h"

#include "source/common/common/assert.h"
#include "source/common/common/hash.h"
#include "source/common/config/utility.h"
#include "source/common/runtime/runtime_features.h"

namespace Envoy {
namespace Config {

DeltaSubscriptionState::DeltaSubscriptionState(std::string type_url,
                                               UntypedConfigUpdateCallbacks& watch_map,
                                               const LocalInfo::LocalInfo& local_info,
                                               Event::Dispatcher& dispatcher, const bool wildcard)
    // TODO(snowp): Hard coding VHDS here is temporary until we can move it away from relying on
    // empty resources as updates.
    : supports_heartbeats_(type_url != "envoy.config.route.v3.VirtualHost"),
      ttl_(
          [this](const auto& expired) {
            Protobuf::RepeatedPtrField<std::string> removed_resources;
            for (const auto& resource : expired) {
              if (auto maybe_resource = getResourceState(resource); maybe_resource.has_value()) {
                maybe_resource->setAsWaitingForServer();
                removed_resources.Add(std::string(resource));
              }
            }

            watch_map_.onConfigUpdate({}, removed_resources, "");
          },
          dispatcher, dispatcher.timeSource()),
      type_url_(std::move(type_url)),
      mode_(wildcard ? WildcardMode::Implicit : WildcardMode::Disabled), watch_map_(watch_map),
      local_info_(local_info), dispatcher_(dispatcher) {}

void DeltaSubscriptionState::updateSubscriptionInterest(
    const absl::flat_hash_set<std::string>& cur_added,
    const absl::flat_hash_set<std::string>& cur_removed) {
  for (const auto& a : cur_added) {
    // This adds a resource state that is waiting for the server for
    // more information.
    resource_state_.insert_or_assign(a, ResourceType::ExplicitlyRequested);
    // If interest in a resource is removed-then-added (all before a discovery request
    // can be sent), we must treat it as a "new" addition: our user may have forgotten its
    // copy of the resource after instructing us to remove it, and need to be reminded of it.
    names_removed_.erase(a);
    names_added_.insert(a);
  }
  for (const auto& r : cur_removed) {
    removeResourceState(r);
    // Ideally, when interest in a resource is added-then-removed in between requests,
    // we would avoid putting a superfluous "unsubscribe [resource that was never subscribed]"
    // in the request. However, the removed-then-added case *does* need to go in the request,
    // and due to how we accomplish that, it's difficult to distinguish remove-add-remove from
    // add-remove (because "remove-add" has to be treated as equivalent to just "add").
    names_added_.erase(r);
    names_removed_.insert(r);
  }
  switch (mode_) {
  case WildcardMode::Implicit:
    if (names_removed_.find("*") != names_removed_.end()) {
      // we explicitly cancel the wildcard subscription
      mode_ = WildcardMode::Disabled;
    } else if (!names_added_.empty()) {
      // switch to explicit mode if we requested some extra names
      mode_ = WildcardMode::Explicit;
    }
    break;

  case WildcardMode::Explicit:
    if (names_removed_.find("*") != names_removed_.end()) {
      // we explicitly cancel the wildcard subscription
      mode_ = WildcardMode::Disabled;
    }
    break;

  case WildcardMode::Disabled:
    if (names_added_.find("*") != names_added_.end()) {
      // we switch into an explicit wildcard subscription
      mode_ = WildcardMode::Explicit;
    }
    break;
  }
}

// Not having sent any requests yet counts as an "update pending" since you're supposed to resend
// the entirety of your interest at the start of a stream, even if nothing has changed.
bool DeltaSubscriptionState::subscriptionUpdatePending() const {
  return !names_added_.empty() || !names_removed_.empty() ||
         !any_request_sent_yet_in_current_stream_ || must_send_discovery_request_;
}

UpdateAck DeltaSubscriptionState::handleResponse(
    const envoy::service::discovery::v3::DeltaDiscoveryResponse& message) {
  // We *always* copy the response's nonce into the next request, even if we're going to make that
  // request a NACK by setting error_detail.
  UpdateAck ack(message.nonce(), type_url_);
  TRY_ASSERT_MAIN_THREAD { handleGoodResponse(message); }
  END_TRY
  catch (const EnvoyException& e) {
    handleBadResponse(e, ack);
  }
  return ack;
}

bool DeltaSubscriptionState::isHeartbeatResponse(
    const envoy::service::discovery::v3::Resource& resource) const {
  if (!supports_heartbeats_ &&
      !Runtime::runtimeFeatureEnabled("envoy.reloadable_features.vhds_heartbeats")) {
    return false;
  }
  const auto itr = resource_state_.find(resource.name());
  if (itr == resource_state_.end()) {
    return false;
  }

  return !resource.has_resource() && !itr->second.waitingForServer() &&
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
    if (isHeartbeatResponse(resource)) {
      continue;
    }
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
      addResourceStateFromServer(resource);
    }
  }

  watch_map_.onConfigUpdate(non_heartbeat_resources, message.removed_resources(),
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
    if (auto maybe_resource = getResourceState(resource_name); maybe_resource.has_value()) {
      maybe_resource->setAsWaitingForServer();
    }
  }
  ENVOY_LOG(debug, "Delta config for {} accepted with {} resources added, {} removed", type_url_,
            message.resources().size(), message.removed_resources().size());
}

void DeltaSubscriptionState::handleBadResponse(const EnvoyException& e, UpdateAck& ack) {
  // Note that error_detail being set is what indicates that a DeltaDiscoveryRequest is a NACK.
  ack.error_detail_.set_code(Grpc::Status::WellKnownGrpcStatus::Internal);
  ack.error_detail_.set_message(Config::Utility::truncateGrpcStatusMessage(e.what()));
  ENVOY_LOG(warn, "delta config for {} rejected: {}", type_url_, e.what());
  watch_map_.onConfigUpdateFailed(Envoy::Config::ConfigUpdateFailureReason::UpdateRejected, &e);
}

void DeltaSubscriptionState::handleEstablishmentFailure() {
  watch_map_.onConfigUpdateFailed(Envoy::Config::ConfigUpdateFailureReason::ConnectionFailure,
                                  nullptr);
}

envoy::service::discovery::v3::DeltaDiscoveryRequest
DeltaSubscriptionState::getNextRequestAckless() {
  envoy::service::discovery::v3::DeltaDiscoveryRequest request;
  must_send_discovery_request_ = false;
  if (!any_request_sent_yet_in_current_stream_) {
    any_request_sent_yet_in_current_stream_ = true;
    // initial_resource_versions "must be populated for first request in a stream".
    // Also, since this might be a new server, we must explicitly state *all* of our subscription
    // interest.
    for (auto const& [resource_name, resource_state] : resource_state_) {
      // Populate initial_resource_versions with the resource versions we currently have.
      // Resources we are interested in, but are still waiting to get any version of from the
      // server, do not belong in initial_resource_versions. (But do belong in new subscriptions!)
      if (!resource_state.waitingForServer()) {
        (*request.mutable_initial_resource_versions())[resource_name] = resource_state.version();
      }
      // Add resource names to resource_names_subscribe only if this is not a wildcard subscription
      // request or if we requested this resource explicitly (so we are actually in explicit
      // wildcard mode).
      if (mode_ == WildcardMode::Disabled ||
          resource_state.type() == ResourceType::ExplicitlyRequested) {
        names_added_.insert(resource_name);
      }
    }
    // We are not clearing the names_added_ set. If we are in implicit wildcard subscription mode,
    // then the set should already be empty. If we are in explicit wildcard mode then the set will
    // contain the names we explicitly requested, but we need to add * to the list to make sure it's
    // sent too.
    if (mode_ == WildcardMode::Explicit) {
      names_added_.insert("*");
    }
    names_removed_.clear();
  }
  std::copy(names_added_.begin(), names_added_.end(),
            Protobuf::RepeatedFieldBackInserter(request.mutable_resource_names_subscribe()));
  std::copy(names_removed_.begin(), names_removed_.end(),
            Protobuf::RepeatedFieldBackInserter(request.mutable_resource_names_unsubscribe()));
  names_added_.clear();
  names_removed_.clear();

  request.set_type_url(type_url_);
  request.mutable_node()->MergeFrom(local_info_.node());
  return request;
}

envoy::service::discovery::v3::DeltaDiscoveryRequest
DeltaSubscriptionState::getNextRequestWithAck(const UpdateAck& ack) {
  envoy::service::discovery::v3::DeltaDiscoveryRequest request = getNextRequestAckless();
  request.set_response_nonce(ack.nonce_);
  if (ack.error_detail_.code() != Grpc::Status::WellKnownGrpcStatus::Ok) {
    // Don't needlessly make the field present-but-empty if status is ok.
    request.mutable_error_detail()->CopyFrom(ack.error_detail_);
  }
  return request;
}

void DeltaSubscriptionState::addResourceStateFromServer(
    const envoy::service::discovery::v3::Resource& resource) {
  if (resource.has_ttl()) {
    ttl_.add(std::chrono::milliseconds(DurationUtil::durationToMilliseconds(resource.ttl())),
             resource.name());
  } else {
    ttl_.clear(resource.name());
  }

  if (auto it = resource_state_.find(resource.name()); it != resource_state_.end()) {
    auto old_type = it->second.type();
    it->second = ResourceState(resource, old_type);
  } else {
    resource_state_.insert(
        {resource.name(), ResourceState(resource, ResourceType::ReceivedFromServer)});
  }
}

OptRef<DeltaSubscriptionState::ResourceState>
DeltaSubscriptionState::getResourceState(const std::string& resource_name) {
  auto itr = resource_state_.find(resource_name);
  if (itr == resource_state_.end()) {
    return {};
  }
  return {itr->second};
}

void DeltaSubscriptionState::removeResourceState(const std::string& resource_name) {
  resource_state_.erase(resource_name);
}

} // namespace Config
} // namespace Envoy
