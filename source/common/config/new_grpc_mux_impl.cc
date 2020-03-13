#include "common/config/new_grpc_mux_impl.h"

#include "envoy/service/discovery/v3/discovery.pb.h"

#include "common/common/assert.h"
#include "common/common/backoff_strategy.h"
#include "common/common/token_bucket_impl.h"
#include "common/config/utility.h"
#include "common/config/version_converter.h"
#include "common/memory/utils.h"
#include "common/protobuf/protobuf.h"
#include "common/protobuf/utility.h"

namespace Envoy {
namespace Config {

NewGrpcMuxImpl::NewGrpcMuxImpl(Grpc::RawAsyncClientPtr&& async_client,
                               Event::Dispatcher& dispatcher,
                               const Protobuf::MethodDescriptor& service_method,
                               envoy::config::core::v3::ApiVersion transport_api_version,
                               Runtime::RandomGenerator& random, Stats::Scope& scope,
                               const RateLimitSettings& rate_limit_settings,
                               const LocalInfo::LocalInfo& local_info)
    : grpc_stream_(this, std::move(async_client), service_method, random, dispatcher, scope,
                   rate_limit_settings),
      local_info_(local_info), transport_api_version_(transport_api_version) {}

void NewGrpcMuxImpl::pause(const std::string& type_url) { pausable_ack_queue_.pause(type_url); }

void NewGrpcMuxImpl::resume(const std::string& type_url) {
  pausable_ack_queue_.resume(type_url);
  trySendDiscoveryRequests();
}

bool NewGrpcMuxImpl::paused(const std::string& type_url) const {
  return pausable_ack_queue_.paused(type_url);
}

void NewGrpcMuxImpl::onDiscoveryResponse(
    std::unique_ptr<envoy::service::discovery::v3::DeltaDiscoveryResponse>&& message) {
  ENVOY_LOG(debug, "Received DeltaDiscoveryResponse for {} at version {}", message->type_url(),
            message->system_version_info());
  auto sub = subscriptions_.find(message->type_url());
  if (sub == subscriptions_.end()) {
    ENVOY_LOG(warn,
              "Dropping received DeltaDiscoveryResponse (with version {}) for non-existent "
              "subscription {}.",
              message->system_version_info(), message->type_url());
    return;
  }

  // When an on-demand request is made a Watch is created using an alias, as the resource name isn't
  // known at that point. When an update containing aliases comes back, we update Watches with
  // resource names.
  for (const auto& r : message->resources()) {
    if (r.aliases_size() > 0) {
      AddedRemoved converted = sub->second->watch_map_.convertAliasWatchesToNameWatches(r);
      sub->second->sub_state_.updateSubscriptionInterest(converted.added_, converted.removed_);
    }
  }

  kickOffAck(sub->second->sub_state_.handleResponse(*message));
  Memory::Utils::tryShrinkHeap();
}

void NewGrpcMuxImpl::onStreamEstablished() {
  for (auto& sub : subscriptions_) {
    sub.second->sub_state_.markStreamFresh();
  }
  trySendDiscoveryRequests();
}

void NewGrpcMuxImpl::onEstablishmentFailure() {
  // If this happens while Envoy is still initializing, the onConfigUpdateFailed() we ultimately
  // call on CDS will cause LDS to start up, which adds to subscriptions_ here. So, to avoid a
  // crash, the iteration needs to dance around a little: collect pointers to all
  // SubscriptionStates, call on all those pointers we haven't yet called on, repeat if there are
  // now more SubscriptionStates.
  absl::flat_hash_map<std::string, DeltaSubscriptionState*> all_subscribed;
  absl::flat_hash_map<std::string, DeltaSubscriptionState*> already_called;
  do {
    for (auto& sub : subscriptions_) {
      all_subscribed[sub.first] = &sub.second->sub_state_;
    }
    for (auto& sub : all_subscribed) {
      if (already_called.insert(sub).second) { // insert succeeded ==> not already called
        sub.second->handleEstablishmentFailure();
      }
    }
  } while (all_subscribed.size() != subscriptions_.size());
}

void NewGrpcMuxImpl::onWriteable() { trySendDiscoveryRequests(); }

void NewGrpcMuxImpl::kickOffAck(UpdateAck ack) {
  pausable_ack_queue_.push(std::move(ack));
  trySendDiscoveryRequests();
}

// TODO(fredlas) to be removed from the GrpcMux interface very soon.
void NewGrpcMuxImpl::start() { grpc_stream_.establishNewStream(); }

GrpcMuxWatchPtr NewGrpcMuxImpl::addWatch(const std::string& type_url,
                                         const std::set<std::string>& resources,
                                         SubscriptionCallbacks& callbacks) {
  auto entry = subscriptions_.find(type_url);
  if (entry == subscriptions_.end()) {
    // We don't yet have a subscription for type_url! Make one!
    addSubscription(type_url);
    return addWatch(type_url, resources, callbacks);
  }

  Watch* watch = entry->second->watch_map_.addWatch(callbacks);
  // updateWatch() queues a discovery request if any of 'resources' are not yet subscribed.
  updateWatch(type_url, watch, resources);
  return std::make_unique<WatchImpl>(type_url, watch, *this);
}

// Updates the list of resource names watched by the given watch. If an added name is new across
// the whole subscription, or if a removed name has no other watch interested in it, then the
// subscription will enqueue and attempt to send an appropriate discovery request.
void NewGrpcMuxImpl::updateWatch(const std::string& type_url, Watch* watch,
                                 const std::set<std::string>& resources) {
  ASSERT(watch != nullptr);
  auto sub = subscriptions_.find(type_url);
  RELEASE_ASSERT(sub != subscriptions_.end(),
                 fmt::format("Watch of {} has no subscription to update.", type_url));
  auto added_removed = sub->second->watch_map_.updateWatchInterest(watch, resources);
  sub->second->sub_state_.updateSubscriptionInterest(added_removed.added_, added_removed.removed_);
  // Tell the server about our change in interest, if any.
  if (sub->second->sub_state_.subscriptionUpdatePending()) {
    trySendDiscoveryRequests();
  }
}

void NewGrpcMuxImpl::removeWatch(const std::string& type_url, Watch* watch) {
  updateWatch(type_url, watch, {});
  auto entry = subscriptions_.find(type_url);
  ASSERT(entry != subscriptions_.end(),
         fmt::format("removeWatch() called for non-existent subscription {}.", type_url));
  entry->second->watch_map_.removeWatch(watch);
}

void NewGrpcMuxImpl::addSubscription(const std::string& type_url) {
  subscriptions_.emplace(type_url, std::make_unique<SubscriptionStuff>(type_url, local_info_));
  subscription_ordering_.emplace_back(type_url);
}

void NewGrpcMuxImpl::trySendDiscoveryRequests() {
  while (true) {
    // Do any of our subscriptions even want to send a request?
    absl::optional<std::string> maybe_request_type = whoWantsToSendDiscoveryRequest();
    if (!maybe_request_type.has_value()) {
      break;
    }
    // If so, which one (by type_url)?
    std::string next_request_type_url = maybe_request_type.value();
    // If we don't have a subscription object for this request's type_url, drop the request.
    auto sub = subscriptions_.find(next_request_type_url);
    RELEASE_ASSERT(sub != subscriptions_.end(),
                   fmt::format("Tried to send discovery request for non-existent subscription {}.",
                               next_request_type_url));

    // Try again later if paused/rate limited/stream down.
    if (!canSendDiscoveryRequest(next_request_type_url)) {
      break;
    }
    envoy::service::discovery::v3::DeltaDiscoveryRequest request;
    // Get our subscription state to generate the appropriate DeltaDiscoveryRequest, and send.
    if (!pausable_ack_queue_.empty()) {
      // Because ACKs take precedence over plain requests, if there is anything in the queue, it's
      // safe to assume it's of the type_url that we're wanting to send.
      request = sub->second->sub_state_.getNextRequestWithAck(pausable_ack_queue_.popFront());
    } else {
      request = sub->second->sub_state_.getNextRequestAckless();
    }
    VersionConverter::prepareMessageForGrpcWire(request, transport_api_version_);
    grpc_stream_.sendMessage(request);
  }
  grpc_stream_.maybeUpdateQueueSizeStat(pausable_ack_queue_.size());
}

// Checks whether external conditions allow sending a DeltaDiscoveryRequest. (Does not check
// whether we *want* to send a DeltaDiscoveryRequest).
bool NewGrpcMuxImpl::canSendDiscoveryRequest(const std::string& type_url) {
  RELEASE_ASSERT(
      !pausable_ack_queue_.paused(type_url),
      fmt::format("canSendDiscoveryRequest() called on paused type_url {}. Pausedness is "
                  "supposed to be filtered out by whoWantsToSendDiscoveryRequest(). ",
                  type_url));

  if (!grpc_stream_.grpcStreamAvailable()) {
    ENVOY_LOG(trace, "No stream available to send a discovery request for {}.", type_url);
    return false;
  } else if (!grpc_stream_.checkRateLimitAllowsDrain()) {
    ENVOY_LOG(trace, "{} discovery request hit rate limit; will try later.", type_url);
    return false;
  }
  return true;
}

// Checks whether we have something to say in a DeltaDiscoveryRequest, which can be an ACK and/or
// a subscription update. (Does not check whether we *can* send that DeltaDiscoveryRequest).
// Returns the type_url we should send the DeltaDiscoveryRequest for (if any).
// First, prioritizes ACKs over non-ACK subscription interest updates.
// Then, prioritizes non-ACK updates in the order the various types
// of subscriptions were activated.
absl::optional<std::string> NewGrpcMuxImpl::whoWantsToSendDiscoveryRequest() {
  // All ACKs are sent before plain updates. trySendDiscoveryRequests() relies on this. So, choose
  // type_url from pausable_ack_queue_ if possible, before looking at pending updates.
  if (!pausable_ack_queue_.empty()) {
    return pausable_ack_queue_.front().type_url_;
  }
  // If we're looking to send multiple non-ACK requests, send them in the order that their
  // subscriptions were initiated.
  for (const auto& sub_type : subscription_ordering_) {
    auto sub = subscriptions_.find(sub_type);
    if (sub != subscriptions_.end() && sub->second->sub_state_.subscriptionUpdatePending() &&
        !pausable_ack_queue_.paused(sub_type)) {
      return sub->first;
    }
  }
  return absl::nullopt;
}

} // namespace Config
} // namespace Envoy
