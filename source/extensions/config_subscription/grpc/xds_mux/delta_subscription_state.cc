#include "source/extensions/config_subscription/grpc/xds_mux/delta_subscription_state.h"

#include <utility>

#include "envoy/event/dispatcher.h"
#include "envoy/service/discovery/v3/discovery.pb.h"

#include "source/common/common/hash.h"
#include "source/common/config/utility.h"
#include "source/common/runtime/runtime_features.h"

#include "absl/container/inlined_vector.h"

namespace Envoy {
namespace Config {
namespace XdsMux {

DeltaSubscriptionState::DeltaSubscriptionState(std::string type_url,
                                               UntypedConfigUpdateCallbacks& watch_map,
                                               Event::Dispatcher& dispatcher,
                                               XdsConfigTrackerOptRef xds_config_tracker,
                                               OptRef<XdsSymbolTable> xds_symbol_table)
    : BaseSubscriptionState(std::move(type_url), watch_map, dispatcher, xds_config_tracker),
      compress_resource_names_(
          Runtime::runtimeFeatureEnabled("envoy.reloadable_features.compress_xds_resource_names")),
      xds_symbol_table_(compress_resource_names_
                            ? (xds_symbol_table.has_value() ? xds_symbol_table->shared_from_this()
                                                            : XdsSymbolTable::create())
                            : nullptr),
      // TODO(snowp): Hard coding VHDS here is temporary until we can move it away from relying on
      // empty resources as updates.
      supports_heartbeats_(type_url_ != "envoy.config.route.v3.VirtualHost") {}

DeltaSubscriptionState::~DeltaSubscriptionState() = default;

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
    if (compress_resource_names_) {
      bool transitioned = false;
      absl::InlinedVector<uint8_t, 64> scratch;
      if (const auto maybe_encoded = xds_symbol_table_->tryEncode(a, scratch);
          maybe_encoded.has_value()) {
        if (auto it = compressed_wildcard_resource_state_.find(*maybe_encoded);
            it != compressed_wildcard_resource_state_.end()) {
          ResourceState state = ResourceState::withVersion(it->second);
          auto node = compressed_wildcard_resource_state_.extract(it);
          compressed_requested_resource_state_.insert_or_assign(std::move(node.key()),
                                                                std::move(state));
          transitioned = true;
        } else if (auto amb_it = compressed_ambiguous_resource_state_.find(*maybe_encoded);
                   amb_it != compressed_ambiguous_resource_state_.end()) {
          ResourceState state = ResourceState::withVersion(amb_it->second);
          auto node = compressed_ambiguous_resource_state_.extract(amb_it);
          compressed_requested_resource_state_.insert_or_assign(std::move(node.key()),
                                                                std::move(state));
          transitioned = true;
        }
      }
      if (!transitioned) {
        compressed_requested_resource_state_.insert_or_assign(xds_symbol_table_->encode(a),
                                                              ResourceState::waitingForServer());
      }
    } else if (auto it = wildcard_resource_state_.find(a); it != wildcard_resource_state_.end()) {
      requested_resource_state_.insert_or_assign(a, ResourceState::withVersion(it->second));
      wildcard_resource_state_.erase(it);
    } else if (it = ambiguous_resource_state_.find(a); it != ambiguous_resource_state_.end()) {
      requested_resource_state_.insert_or_assign(a, ResourceState::withVersion(it->second));
      ambiguous_resource_state_.erase(it);
    } else {
      requested_resource_state_.insert_or_assign(a, ResourceState::waitingForServer());
    }
    ASSERT(containsRequestedResource(a));
    ASSERT(!getWildcardResourceVersion(a).has_value());
    ASSERT(!getAmbiguousResourceVersion(a).has_value());
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
    if (compress_resource_names_) {
      absl::InlinedVector<uint8_t, 64> scratch;
      if (const auto maybe_encoded = xds_symbol_table_->tryEncode(r, scratch);
          maybe_encoded.has_value()) {
        if (containsRequestedResource(Wildcard)) {
          if (auto it = compressed_requested_resource_state_.find(*maybe_encoded);
              it != compressed_requested_resource_state_.end()) {
            // Wildcard resources always have a version. If our requested resource has no version,
            // it won't be a wildcard resource then. If r is Wildcard itself, then it never has a
            // version attached to it, so it will not be moved to ambiguous category.
            if (!it->second.isWaitingForServer()) {
              std::string version = it->second.version();
              auto node = compressed_requested_resource_state_.extract(it);
              compressed_ambiguous_resource_state_.insert_or_assign(std::move(node.key()),
                                                                    std::move(version));
            } else {
              compressed_requested_resource_state_.erase(it);
            }
            actually_erased = true;
          }
        } else {
          actually_erased = (compressed_requested_resource_state_.erase(*maybe_encoded) > 0);
        }
      }
    } else if (requested_resource_state_.contains(Wildcard)) {
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
    ASSERT(!containsRequestedResource(r));
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
  // cache.
  if (cur_removed.contains(Wildcard)) {
    if (compress_resource_names_) {
      compressed_wildcard_resource_state_.clear();
      compressed_ambiguous_resource_state_.clear();
    } else {
      wildcard_resource_state_.clear();
      ambiguous_resource_state_.clear();
    }
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
    if (!requestedResourceStateEmpty()) {
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
  return dynamicContextChanged();
}

void DeltaSubscriptionState::markStreamFresh(bool should_send_initial_resource_versions) {
  any_request_sent_yet_in_current_stream_ = false;
  should_send_initial_resource_versions_ = should_send_initial_resource_versions;
}

bool DeltaSubscriptionState::isHeartbeatResource(
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

  if (const auto maybe_version = getWildcardResourceVersion(resource.name());
      maybe_version.has_value()) {
    return resource.version() == *maybe_version;
  }

  if (const auto maybe_version = getAmbiguousResourceVersion(resource.name());
      maybe_version.has_value()) {
    // In theory we should move the ambiguous resource to wildcard, because probably we shouldn't be
    // getting heartbeat responses about resources that we are not interested in, but the server
    // could have sent this heartbeat before it learned about our lack of interest in the resource.
    return resource.version() == *maybe_version;
  }

  return false;
}

void DeltaSubscriptionState::handleGoodResponse(
    envoy::service::discovery::v3::DeltaDiscoveryResponse& message) {
  absl::flat_hash_set<std::string> names_added_removed;

  for (const auto& resource : message.resources()) {
    if (!names_added_removed.insert(resource.name()).second) {
      throwEnvoyExceptionOrPanic(
          fmt::format("duplicate name {} found among added/updated resources", resource.name()));
    }
    if (isHeartbeatResource(resource)) {
      continue;
    }
    // DeltaDiscoveryResponses for unresolved aliases don't contain an actual resource
    if (!resource.has_resource() && resource.aliases_size() > 0) {
      continue;
    }
    if (message.type_url() != resource.resource().type_url()) {
      throwEnvoyExceptionOrPanic(
          fmt::format("type URL {} embedded in an individual Any does not match "
                      "the message-wide type URL {} in DeltaDiscoveryResponse {}",
                      resource.resource().type_url(), message.type_url(), message.DebugString()));
    }
  }
  for (const auto& name : message.removed_resources()) {
    if (!names_added_removed.insert(name).second) {
      throwEnvoyExceptionOrPanic(
          fmt::format("duplicate name {} found in the union of added+removed resources", name));
    }
  }

  // Reorder the resources in the response, having all the non-heartbeat
  // resources at the front of the list. Note that although there's no
  // requirement to keep stable ordering, we do so to process the resources in
  // the order they were sent.
  auto last_non_heartbeat = std::stable_partition(
      message.mutable_resources()->begin(), message.mutable_resources()->end(),
      [&](const envoy::service::discovery::v3::Resource& resource) {
        return !isHeartbeatResource(resource);
      });

  auto non_heartbeat_resources_span = absl::MakeConstSpan(
      message.resources().data(), last_non_heartbeat - message.resources().begin());
  callbacks().onConfigUpdate(non_heartbeat_resources_span, message.removed_resources(),
                             message.system_version_info());

  // Processing point when resources are successfully ingested.
  if (xds_config_tracker_.has_value()) {
    xds_config_tracker_->onConfigAccepted(message.type_url(), non_heartbeat_resources_span,
                                          message.removed_resources());
  }

  {
    const auto scoped_update = ttl_.scopedTtlUpdate();
    if (containsRequestedResource(Wildcard)) {
      for (const auto& resource : message.resources()) {
        addResourceStateFromServer(resource);
      }
    } else {
      // We are not subscribed to wildcard, so we only take resources that we explicitly requested
      // and ignore the others.
      // NOTE: This is not gonna work for xdstp resources with glob resource matching.
      for (const auto& resource : message.resources()) {
        if (containsRequestedResource(resource.name())) {
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
    } else {
      eraseWildcardOrAmbiguousResource(resource_name);
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
    const bool is_legacy_wildcard = isInitialRequestForLegacyWildcard();
    // initial_resource_versions "must be populated for first request in a stream".
    // Also, since this might be a new server, we must explicitly state *all* of our subscription
    // interest.
    if (compress_resource_names_) {
      for (auto const& [resource_name, resource_state] : compressed_requested_resource_state_) {
        std::string decoded_name = xds_symbol_table_->toString(resource_name.resourceName());
        if (should_send_initial_resource_versions_ && !resource_state.isWaitingForServer()) {
          (*request->mutable_initial_resource_versions())[decoded_name] = resource_state.version();
        }
        names_added_.insert(std::move(decoded_name));
      }
      if (should_send_initial_resource_versions_) {
        for (auto const& [resource_name, resource_version] : compressed_wildcard_resource_state_) {
          (*request->mutable_initial_resource_versions())[xds_symbol_table_->toString(
              resource_name.resourceName())] = resource_version;
        }
        for (auto const& [resource_name, resource_version] : compressed_ambiguous_resource_state_) {
          (*request->mutable_initial_resource_versions())[xds_symbol_table_->toString(
              resource_name.resourceName())] = resource_version;
        }
      }
    } else {
      for (auto const& [resource_name, resource_state] : requested_resource_state_) {
        if (should_send_initial_resource_versions_) {
          // Populate initial_resource_versions with the resource versions we currently have.
          // Resources we are interested in, but are still waiting to get any version of from the
          // server, do not belong in initial_resource_versions. (But do belong in new
          // subscriptions!)
          if (!resource_state.isWaitingForServer()) {
            (*request->mutable_initial_resource_versions())[resource_name] =
                resource_state.version();
          }
        }
        // We are going over a list of resources that we are interested in, so add them to
        // resource_names_subscribe.
        names_added_.insert(resource_name);
      }
      if (should_send_initial_resource_versions_) {
        for (auto const& [resource_name, resource_version] : wildcard_resource_state_) {
          (*request->mutable_initial_resource_versions())[resource_name] = resource_version;
        }
        for (auto const& [resource_name, resource_version] : ambiguous_resource_state_) {
          (*request->mutable_initial_resource_versions())[resource_name] = resource_version;
        }
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
            Protobuf::RepeatedFieldBackInserter(request->mutable_resource_names_subscribe()));
  std::copy(names_removed_.begin(), names_removed_.end(),
            Protobuf::RepeatedFieldBackInserter(request->mutable_resource_names_unsubscribe()));
  names_added_.clear();
  names_removed_.clear();

  return request;
}

bool DeltaSubscriptionState::isInitialRequestForLegacyWildcard() {
  if (in_initial_legacy_wildcard_) {
    if (compress_resource_names_) {
      compressed_requested_resource_state_.insert_or_assign(xds_symbol_table_->encode(Wildcard),
                                                            ResourceState::waitingForServer());
    } else {
      requested_resource_state_.insert_or_assign(Wildcard, ResourceState::waitingForServer());
    }
    ASSERT(containsRequestedResource(Wildcard));
    ASSERT(!getWildcardResourceVersion(Wildcard).has_value());
    ASSERT(!getAmbiguousResourceVersion(Wildcard).has_value());
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
  ASSERT(!requestedResourceStateEmpty());

  // If our subscription interest didn't change then the first condition for using legacy wildcard
  // subscription is met.
  if (!names_added_.empty() || !names_removed_.empty()) {
    return false;
  }
  // If we requested only a wildcard resource then the second condition for using legacy wildcard
  // condition is met.
  if (compress_resource_names_) {
    return compressed_requested_resource_state_.size() == 1 && containsRequestedResource(Wildcard);
  }
  return requested_resource_state_.size() == 1 &&
         requested_resource_state_.begin()->first == Wildcard;
}

void DeltaSubscriptionState::addResourceStateFromServer(
    const envoy::service::discovery::v3::Resource& resource) {
  setResourceTtl(resource);

  if (auto maybe_resource = getRequestedResourceState(resource.name());
      maybe_resource.has_value()) {
    // It is a resource that we requested.
    maybe_resource->setVersion(resource.version());
    ASSERT(containsRequestedResource(resource.name()));
    ASSERT(!getWildcardResourceVersion(resource.name()).has_value());
    ASSERT(!getAmbiguousResourceVersion(resource.name()).has_value());
  } else {
    // It is a resource that is a part of our wildcard request.
    if (compress_resource_names_) {
      bool transitioned = false;
      absl::InlinedVector<uint8_t, 64> scratch;
      if (const auto maybe_encoded = xds_symbol_table_->tryEncode(resource.name(), scratch);
          maybe_encoded.has_value()) {
        if (auto amb_it = compressed_ambiguous_resource_state_.find(*maybe_encoded);
            amb_it != compressed_ambiguous_resource_state_.end()) {
          auto node = compressed_ambiguous_resource_state_.extract(amb_it);
          compressed_wildcard_resource_state_.insert_or_assign(std::move(node.key()),
                                                               resource.version());
          transitioned = true;
        }
      }
      if (!transitioned) {
        compressed_wildcard_resource_state_.insert_or_assign(
            xds_symbol_table_->encode(resource.name()), resource.version());
      }
    } else {
      wildcard_resource_state_.insert_or_assign(resource.name(), resource.version());
      // The resource could be ambiguous before, but now the ambiguity
      // is resolved.
      ambiguous_resource_state_.erase(resource.name());
    }
    ASSERT(!containsRequestedResource(resource.name()));
    ASSERT(getWildcardResourceVersion(resource.name()).has_value());
    ASSERT(!getAmbiguousResourceVersion(resource.name()).has_value());
  }
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
    if (auto maybe_resource = getRequestedResourceState(resource); maybe_resource.has_value()) {
      maybe_resource->setAsWaitingForServer();
      removed_resources.Add(std::string(resource));
    } else if (eraseWildcardOrAmbiguousResource(resource)) {
      removed_resources.Add(std::string(resource));
    }
  }
  callbacks().onConfigUpdate({}, removed_resources, "");
}

OptRef<DeltaSubscriptionState::ResourceState>
DeltaSubscriptionState::getRequestedResourceState(absl::string_view resource_name) {
  if (compress_resource_names_) {
    absl::InlinedVector<uint8_t, 64> scratch;
    const auto encoded = xds_symbol_table_->tryEncode(resource_name, scratch);
    if (!encoded.has_value()) {
      return {};
    }
    auto itr = compressed_requested_resource_state_.find(*encoded);
    if (itr == compressed_requested_resource_state_.end()) {
      return {};
    }
    return {itr->second};
  }
  auto itr = requested_resource_state_.find(resource_name);
  if (itr == requested_resource_state_.end()) {
    return {};
  }
  return {itr->second};
}

OptRef<const DeltaSubscriptionState::ResourceState>
DeltaSubscriptionState::getRequestedResourceState(absl::string_view resource_name) const {
  if (compress_resource_names_) {
    absl::InlinedVector<uint8_t, 64> scratch;
    const auto encoded = xds_symbol_table_->tryEncode(resource_name, scratch);
    if (!encoded.has_value()) {
      return {};
    }
    auto itr = compressed_requested_resource_state_.find(*encoded);
    if (itr == compressed_requested_resource_state_.end()) {
      return {};
    }
    return {itr->second};
  }
  auto itr = requested_resource_state_.find(resource_name);
  if (itr == requested_resource_state_.end()) {
    return {};
  }
  return {itr->second};
}

OptRef<const std::string>
DeltaSubscriptionState::getWildcardResourceVersion(absl::string_view resource_name) const {
  if (compress_resource_names_) {
    absl::InlinedVector<uint8_t, 64> scratch;
    const auto encoded = xds_symbol_table_->tryEncode(resource_name, scratch);
    if (!encoded.has_value()) {
      return {};
    }
    auto itr = compressed_wildcard_resource_state_.find(*encoded);
    if (itr == compressed_wildcard_resource_state_.end()) {
      return {};
    }
    return {itr->second};
  }
  auto itr = wildcard_resource_state_.find(resource_name);
  if (itr == wildcard_resource_state_.end()) {
    return {};
  }
  return {itr->second};
}

OptRef<const std::string>
DeltaSubscriptionState::getAmbiguousResourceVersion(absl::string_view resource_name) const {
  if (compress_resource_names_) {
    absl::InlinedVector<uint8_t, 64> scratch;
    const auto encoded = xds_symbol_table_->tryEncode(resource_name, scratch);
    if (!encoded.has_value()) {
      return {};
    }
    auto itr = compressed_ambiguous_resource_state_.find(*encoded);
    if (itr == compressed_ambiguous_resource_state_.end()) {
      return {};
    }
    return {itr->second};
  }
  auto itr = ambiguous_resource_state_.find(resource_name);
  if (itr == ambiguous_resource_state_.end()) {
    return {};
  }
  return {itr->second};
}

bool DeltaSubscriptionState::eraseWildcardOrAmbiguousResource(absl::string_view resource_name) {
  if (compress_resource_names_) {
    absl::InlinedVector<uint8_t, 64> scratch;
    const auto encoded = xds_symbol_table_->tryEncode(resource_name, scratch);
    if (!encoded.has_value()) {
      return false;
    }
    const size_t erased = compressed_wildcard_resource_state_.erase(*encoded) +
                          compressed_ambiguous_resource_state_.erase(*encoded);
    return erased > 0;
  }
  const size_t erased = wildcard_resource_state_.erase(resource_name) +
                        ambiguous_resource_state_.erase(resource_name);
  return erased > 0;
}

} // namespace XdsMux
} // namespace Config
} // namespace Envoy
