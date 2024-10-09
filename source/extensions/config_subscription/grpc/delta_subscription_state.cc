#include "source/extensions/config_subscription/grpc/delta_subscription_state.h"

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
                                               Event::Dispatcher& dispatcher,
                                               XdsConfigTrackerOptRef xds_config_tracker)
    // TODO(snowp): Hard coding VHDS here is temporary until we can move it away from relying on
    // empty resources as updates.
    : supports_heartbeats_(type_url != "envoy.config.route.v3.VirtualHost"),
      ttl_(
          [this](const auto& expired) {
            Protobuf::RepeatedPtrField<std::string> removed_resources;
            for (const auto& resource : expired) {
              if (auto maybe_resource = getRequestedResourceState(resource);
                  maybe_resource.has_value()) {
                maybe_resource->setAsWaitingForServer();
                removed_resources.Add(std::string(resource));
              } else if (const auto erased_count = wildcard_resource_state_.erase(resource) +
                                                   ambiguous_resource_state_.erase(resource);
                         erased_count > 0) {
                removed_resources.Add(std::string(resource));
              }
            }

            watch_map_.onConfigUpdate({}, removed_resources, "");
          },
          dispatcher, dispatcher.timeSource()),
      type_url_(std::move(type_url)), watch_map_(watch_map), local_info_(local_info),
      xds_config_tracker_(xds_config_tracker) {}

void DeltaSubscriptionState::updateSubscriptionInterest(
    const absl::flat_hash_set<std::string>& cur_added,
    const absl::flat_hash_set<std::string>& cur_removed) {
  for (const auto& a : cur_added) {
    if (in_initial_legacy_wildcard_ && a != Wildcard) {
      in_initial_legacy_wildcard_ = false;
    }
    // If the requested resource existed as a wildcard resource,
    // transition it to requested. Otherwise mark it as a resource
    // waiting for the server to receive the version.
    if (auto it = wildcard_resource_state_.find(a); it != wildcard_resource_state_.end()) {
      requested_resource_state_.insert_or_assign(a, ResourceState::withVersion(it->second));
      wildcard_resource_state_.erase(it);
    } else if (it = ambiguous_resource_state_.find(a); it != ambiguous_resource_state_.end()) {
      requested_resource_state_.insert_or_assign(a, ResourceState::withVersion(it->second));
      ambiguous_resource_state_.erase(it);
    } else {
      requested_resource_state_.insert_or_assign(a, ResourceState::waitingForServer());
    }
    ASSERT(requested_resource_state_.contains(a));
    ASSERT(!wildcard_resource_state_.contains(a));
    ASSERT(!ambiguous_resource_state_.contains(a));
    // If interest in a resource is removed-then-added (all before a discovery request
    // can be sent), we must treat it as a "new" addition: our user may have forgotten its
    // copy of the resource after instructing us to remove it, and need to be reminded of it.
    names_removed_.erase(a);
    names_added_.insert(a);
  }
  for (const auto& r : cur_removed) {
    auto actually_erased = false;
    // The resource we have lost the interest in could also come from our wildcard subscription. We
    // just don't know it at this point. Instead of removing it outright, mark the resource as not
    // interesting to us any more and the server will send us an update. If we don't have a wildcard
    // subscription then there is no ambiguity and just drop the resource.
    if (requested_resource_state_.contains(Wildcard)) {
      if (auto it = requested_resource_state_.find(r); it != requested_resource_state_.end()) {
        // Wildcard resources always have a version. If our requested resource has no version, it
        // won't be a wildcard resource then. If r is Wildcard itself, then it never has a version
        // attached to it, so it will not be moved to ambiguous category.
        if (!it->second.isWaitingForServer()) {
          ambiguous_resource_state_.insert_or_assign(it->first, it->second.version());
        }
        requested_resource_state_.erase(it);
        actually_erased = true;
      }
    } else {
      actually_erased = (requested_resource_state_.erase(r) > 0);
    }
    ASSERT(!requested_resource_state_.contains(r));
    // Ideally, when interest in a resource is added-then-removed in between requests,
    // we would avoid putting a superfluous "unsubscribe [resource that was never subscribed]"
    // in the request. However, the removed-then-added case *does* need to go in the request,
    // and due to how we accomplish that, it's difficult to distinguish remove-add-remove from
    // add-remove (because "remove-add" has to be treated as equivalent to just "add").
    names_added_.erase(r);
    if (actually_erased) {
      names_removed_.insert(r);
      in_initial_legacy_wildcard_ = false;
    }
  }
  // If we unsubscribe from wildcard resource, drop all the resources that came from wildcard from
  // cache. Also drop the ambiguous resources - we aren't interested in those, but we didn't know if
  // those came from wildcard subscription or not, but now it's not important any more.
  if (cur_removed.contains(Wildcard)) {
    wildcard_resource_state_.clear();
    ambiguous_resource_state_.clear();
  }
}

// Not having sent any requests yet counts as an "update pending" since you're supposed to resend
// the entirety of your interest at the start of a stream, even if nothing has changed.
bool DeltaSubscriptionState::subscriptionUpdatePending() const {
  if (!names_added_.empty() || !names_removed_.empty()) {
    return true;
  }
  // At this point, we have no new resources to subscribe to or any
  // resources to unsubscribe from.
  if (!any_request_sent_yet_in_current_stream_) {
    // If we haven't sent anything on the current stream, but we are actually interested in some
    // resource then we obviously need to let the server know about those.
    if (!requested_resource_state_.empty()) {
      return true;
    }
    // So there are no new names and we are interested in nothing. This may either mean that we want
    // the legacy wildcard subscription to kick in or we actually unsubscribed from everything. If
    // the latter is true, then we should not be sending any requests. In such case the initial
    // wildcard mode will be false. Otherwise it means that the legacy wildcard request should be
    // sent.
    return in_initial_legacy_wildcard_;
  }

  // At this point, we have no changes in subscription resources and this isn't a first request in
  // the stream, so even if there are no resources we are interested in, we can send the request,
  // because even if it's empty, it won't be interpreted as legacy wildcard subscription, which can
  // only for the first request in the stream. So sending an empty request at this point should be
  // harmless.
  return must_send_discovery_request_;
}

void DeltaSubscriptionState::markStreamFresh(bool should_send_initial_resource_versions) {
  any_request_sent_yet_in_current_stream_ = false;
  should_send_initial_resource_versions_ = should_send_initial_resource_versions;
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
  if (!supports_heartbeats_) {
    return false;
  }
  if (resource.has_resource()) {
    return false;
  }

  if (const auto maybe_resource = getRequestedResourceState(resource.name());
      maybe_resource.has_value()) {
    return !maybe_resource->isWaitingForServer() && resource.version() == maybe_resource->version();
  }

  if (const auto itr = wildcard_resource_state_.find(resource.name());
      itr != wildcard_resource_state_.end()) {
    return resource.version() == itr->second;
  }

  if (const auto itr = ambiguous_resource_state_.find(resource.name());
      itr != ambiguous_resource_state_.end()) {
    // In theory we should move the ambiguous resource to wildcard, because probably we shouldn't be
    // getting heartbeat responses about resources that we are not interested in, but the server
    // could have sent this heartbeat before it learned about our lack of interest in the resource.
    return resource.version() == itr->second;
  }

  return false;
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

  absl::Span<const envoy::service::discovery::v3::Resource* const> non_heartbeat_resources_span =
      absl::MakeConstSpan(non_heartbeat_resources.data(), non_heartbeat_resources.size());
  watch_map_.onConfigUpdate(non_heartbeat_resources_span, message.removed_resources(),
                            message.system_version_info());

  // Processing point when resources are successfully ingested.
  if (xds_config_tracker_.has_value()) {
    xds_config_tracker_->onConfigAccepted(message.type_url(), non_heartbeat_resources_span,
                                          message.removed_resources());
  }

  {
    const auto scoped_update = ttl_.scopedTtlUpdate();
    if (requested_resource_state_.contains(Wildcard)) {
      for (const auto& resource : message.resources()) {
        addResourceStateFromServer(resource);
      }
    } else {
      // We are not subscribed to wildcard, so we only take resources that we explicitly requested
      // and ignore the others.
      for (const auto& resource : message.resources()) {
        if (requested_resource_state_.contains(resource.name())) {
          addResourceStateFromServer(resource);
        }
      }
    }
  }

  // If a resource is gone, there is no longer a meaningful version for it that makes sense to
  // provide to the server upon stream reconnect: either it will continue to not exist, in which
  // case saying nothing is fine, or the server will bring back something new, which we should
  // receive regardless (which is the logic that not specifying a version will get you).
  //
  // So, leave the version map entry present but blank if we are still interested in the resource.
  // It will be left out of initial_resource_versions messages, but will remind us to explicitly
  // tell the server "I'm cancelling my subscription" when we lose interest. In case of resources
  // received as a part of the wildcard subscription or resources we already lost interest in, we
  // just drop them.
  for (const auto& resource_name : message.removed_resources()) {
    if (auto maybe_resource = getRequestedResourceState(resource_name);
        maybe_resource.has_value()) {
      maybe_resource->setAsWaitingForServer();
    } else if (const auto erased_count = ambiguous_resource_state_.erase(resource_name);
               erased_count == 0) {
      wildcard_resource_state_.erase(resource_name);
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
    const bool is_legacy_wildcard = isInitialRequestForLegacyWildcard();
    // initial_resource_versions "must be populated for first request in a stream".
    // Also, since this might be a new server, we must explicitly state *all* of our subscription
    // interest.
    for (auto const& [resource_name, resource_state] : requested_resource_state_) {
      if (should_send_initial_resource_versions_) {
        // Populate initial_resource_versions with the resource versions we currently have.
        // Resources we are interested in, but are still waiting to get any version of from the
        // server, do not belong in initial_resource_versions. (But do belong in new subscriptions!)
        if (!resource_state.isWaitingForServer()) {
          (*request.mutable_initial_resource_versions())[resource_name] = resource_state.version();
        }
      }
      // We are going over a list of resources that we are interested in, so add them to
      // resource_names_subscribe.
      names_added_.insert(resource_name);
    }
    if (should_send_initial_resource_versions_) {
      for (auto const& [resource_name, resource_version] : wildcard_resource_state_) {
        (*request.mutable_initial_resource_versions())[resource_name] = resource_version;
      }
      for (auto const& [resource_name, resource_version] : ambiguous_resource_state_) {
        (*request.mutable_initial_resource_versions())[resource_name] = resource_version;
      }
    }
    // If this is a legacy wildcard request, then make sure that the resource_names_subscribe is
    // empty.
    if (is_legacy_wildcard) {
      names_added_.clear();
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

bool DeltaSubscriptionState::isInitialRequestForLegacyWildcard() {
  if (in_initial_legacy_wildcard_) {
    requested_resource_state_.insert_or_assign(Wildcard, ResourceState::waitingForServer());
    ASSERT(requested_resource_state_.contains(Wildcard));
    ASSERT(!wildcard_resource_state_.contains(Wildcard));
    ASSERT(!ambiguous_resource_state_.contains(Wildcard));
    return true;
  }

  // If we are here, this means that we lost our initial wildcard mode, because we subscribed to
  // something in the past. We could still be in the situation now that all we are subscribed to now
  // is wildcard resource, so in such case try to send a legacy wildcard subscription request
  // anyway. For this to happen, two conditions need to apply:
  //
  // 1. No change in interest.
  // 2. The only requested resource is Wildcard resource.
  //
  // The invariant of the code here is that this code is executed only when
  // subscriptionUpdatePending actually returns true, which in our case can only happen if the
  // requested resources state_ isn't empty.
  ASSERT(!requested_resource_state_.empty());

  // If our subscription interest didn't change then the first condition for using legacy wildcard
  // subscription is met.
  if (!names_added_.empty() || !names_removed_.empty()) {
    return false;
  }
  // If we requested only a wildcard resource then the second condition for using legacy wildcard
  // condition is met.
  return requested_resource_state_.size() == 1 &&
         requested_resource_state_.begin()->first == Wildcard;
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

  if (auto maybe_resource = getRequestedResourceState(resource.name());
      maybe_resource.has_value()) {
    // It is a resource that we requested.
    maybe_resource->setVersion(resource.version());
    ASSERT(requested_resource_state_.contains(resource.name()));
    ASSERT(!wildcard_resource_state_.contains(resource.name()));
    ASSERT(!ambiguous_resource_state_.contains(resource.name()));
  } else {
    // It is a resource that is a part of our wildcard request.
    wildcard_resource_state_.insert_or_assign(resource.name(), resource.version());
    // The resource could be ambiguous before, but now the ambiguity
    // is resolved.
    ambiguous_resource_state_.erase(resource.name());
    ASSERT(!requested_resource_state_.contains(resource.name()));
    ASSERT(wildcard_resource_state_.contains(resource.name()));
    ASSERT(!ambiguous_resource_state_.contains(resource.name()));
  }
}

OptRef<DeltaSubscriptionState::ResourceState>
DeltaSubscriptionState::getRequestedResourceState(absl::string_view resource_name) {
  auto itr = requested_resource_state_.find(resource_name);
  if (itr == requested_resource_state_.end()) {
    return {};
  }
  return {itr->second};
}

OptRef<const DeltaSubscriptionState::ResourceState>
DeltaSubscriptionState::getRequestedResourceState(absl::string_view resource_name) const {
  auto itr = requested_resource_state_.find(resource_name);
  if (itr == requested_resource_state_.end()) {
    return {};
  }
  return {itr->second};
}

} // namespace Config
} // namespace Envoy
