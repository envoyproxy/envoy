#include "common/config/grpc_mux_impl.h"

#include "common/common/assert.h"
#include "common/common/backoff_strategy.h"
#include "common/common/token_bucket_impl.h"
#include "common/config/utility.h"
#include "common/protobuf/protobuf.h"
#include "common/protobuf/utility.h"

namespace Envoy {
namespace Config {

GrpcMuxImpl::GrpcMuxImpl(std::unique_ptr<SubscriptionStateFactory> subscription_state_factory,
                         bool skip_subsequent_node, const LocalInfo::LocalInfo& local_info)
    : subscription_state_factory_(std::move(subscription_state_factory)),
      skip_subsequent_node_(skip_subsequent_node), local_info_(local_info) {
  Config::Utility::checkLocalInfo("ads", local_info);
}

Watch* GrpcMuxImpl::addOrUpdateWatch(const std::string& type_url, Watch* watch,
                                     const std::set<std::string>& resources,
                                     SubscriptionCallbacks& callbacks,
                                     std::chrono::milliseconds init_fetch_timeout) {
  if (watch == nullptr) {
    return addWatch(type_url, resources, callbacks, init_fetch_timeout);
  } else {
    updateWatch(type_url, watch, resources);
    return watch;
  }
}

void GrpcMuxImpl::removeWatch(const std::string& type_url, Watch* watch) {
  updateWatch(type_url, watch, {});
  watchMapFor(type_url).removeWatch(watch);
}

void GrpcMuxImpl::pause(const std::string& type_url) { pausable_ack_queue_.pause(type_url); }

void GrpcMuxImpl::resume(const std::string& type_url) {
  pausable_ack_queue_.resume(type_url);
  trySendDiscoveryRequests();
}

bool GrpcMuxImpl::paused(const std::string& type_url) const {
  return pausable_ack_queue_.paused(type_url);
}

void GrpcMuxImpl::start() { establishGrpcStream(); }

void GrpcMuxImpl::disableInitFetchTimeoutTimer() {
  for (auto& sub : subscriptions_) {
    sub.second->disableInitFetchTimeoutTimer();
  }
}

Watch* GrpcMuxImpl::addWatch(const std::string& type_url, const std::set<std::string>& resources,
                             SubscriptionCallbacks& callbacks,
                             std::chrono::milliseconds init_fetch_timeout) {
  auto watch_map = watch_maps_.find(type_url);
  if (watch_map == watch_maps_.end()) {
    // We don't yet have a subscription for type_url! Make one!
    addSubscription(type_url, init_fetch_timeout);
    return addWatch(type_url, resources, callbacks, init_fetch_timeout);
  }

  Watch* watch = watch_map->second->addWatch(callbacks);
  // updateWatch() queues a discovery request if any of 'resources' are not yet subscribed.
  updateWatch(type_url, watch, resources);
  return watch;
}

// Updates the list of resource names watched by the given watch. If an added name is new across
// the whole subscription, or if a removed name has no other watch interested in it, then the
// subscription will enqueue and attempt to send an appropriate discovery request.
void GrpcMuxImpl::updateWatch(const std::string& type_url, Watch* watch,
                              const std::set<std::string>& resources) {
  ASSERT(watch != nullptr);
  auto* sub = subscriptionStateFor(type_url);
  WatchMap& watch_map = watchMapFor(type_url);

  auto added_removed = watch_map.updateWatchInterest(watch, resources);
  sub->updateSubscriptionInterest(added_removed.added_, added_removed.removed_);

  // Tell the server about our new interests, if there are any.
  if (sub->subscriptionUpdatePending()) {
    trySendDiscoveryRequests();
  }
}

void GrpcMuxImpl::addSubscription(const std::string& type_url,
                                  std::chrono::milliseconds init_fetch_timeout) {
  watch_maps_.emplace(type_url, std::make_unique<WatchMap>());
  subscriptions_.emplace(type_url, subscription_state_factory_->makeSubscriptionState(
                                       type_url, *watch_maps_[type_url], init_fetch_timeout));
  subscription_ordering_.emplace_back(type_url);
}

SubscriptionState* GrpcMuxImpl::subscriptionStateFor(const std::string& type_url) {
  auto sub = subscriptions_.find(type_url);
  if (sub == subscriptions_.end()) {
    throw EnvoyException("Tried to look up state for non-existent subscription " + type_url);
  }
  return sub->second.get();
}

WatchMap& GrpcMuxImpl::watchMapFor(const std::string& type_url) {
  auto watch_map = watch_maps_.find(type_url);
  if (watch_map == watch_maps_.end()) {
    throw EnvoyException("Tried to look up WatchMap for non-existent subscription " + type_url);
  }
  return *watch_map->second;
}

void GrpcMuxImpl::handleEstablishedStream() {
  for (auto& sub : subscriptions_) {
    sub.second->markStreamFresh();
  }
  set_any_request_sent_yet_in_current_stream(false);
  trySendDiscoveryRequests();
}

void GrpcMuxImpl::handleStreamEstablishmentFailure() {
  // If this happens while Envoy is still initializing, the onConfigUpdateFailed() we ultimately
  // call on CDS will cause LDS to start up, which adds to subscriptions_ here. So, to avoid a
  // crash, the iteration needs to dance around a little: collect pointers to all
  // SubscriptionStates, call on all those pointers we haven't yet called on, repeat if there are
  // now more SubscriptionStates.
  absl::flat_hash_map<std::string, SubscriptionState*> all_subscribed;
  absl::flat_hash_map<std::string, SubscriptionState*> already_called;
  do {
    for (auto& sub : subscriptions_) {
      all_subscribed[sub.first] = sub.second.get();
    }
    for (auto& sub : all_subscribed) {
      if (already_called.insert(sub).second) { // insert succeeded ==> not already called
        sub.second->handleEstablishmentFailure();
      }
    }
  } while (all_subscribed.size() != subscriptions_.size());
}

void GrpcMuxImpl::genericHandleResponse(const std::string& type_url,
                                        const void* response_proto_ptr) {
  auto sub = subscriptions_.find(type_url);
  if (sub == subscriptions_.end()) {
    ENVOY_LOG(warn,
              "The server sent an xDS response proto with type_url {}, which we have "
              "not subscribed to. Ignoring.",
              type_url);
    return;
  }
  pausable_ack_queue_.push(sub->second->handleResponse(response_proto_ptr));
  trySendDiscoveryRequests();
}

void GrpcMuxImpl::trySendDiscoveryRequests() {
  while (true) {
    // Do any of our subscriptions even want to send a request?
    absl::optional<std::string> maybe_request_type = whoWantsToSendDiscoveryRequest();
    if (!maybe_request_type.has_value()) {
      break;
    }
    // If so, which one (by type_url)?
    std::string next_request_type_url = maybe_request_type.value();
    // If we don't have a subscription object for this request's type_url, drop the request.
    auto* sub = subscriptionStateFor(next_request_type_url);
    // Try again later if paused/rate limited/stream down.
    if (!canSendDiscoveryRequest(next_request_type_url)) {
      break;
    }
    // Get our subscription state to generate the appropriate DeltaDiscoveryRequest, and send.
    if (!pausable_ack_queue_.empty()) {
      // Because ACKs take precedence over plain requests, if there is anything in the queue, it's
      // safe to assume it's of the type_url that we're wanting to send.
      UpdateAck ack = pausable_ack_queue_.front();
      pausable_ack_queue_.pop();
      // getNextRequestWithAck() returns a raw unowned pointer, which sendGrpcMessage deletes.
      sendGrpcMessage(sub->getNextRequestWithAck(ack));
    } else {
      // getNextRequestAckless() returns a raw unowned pointer, which sendGrpcMessage deletes.
      sendGrpcMessage(sub->getNextRequestAckless());
    }
  }
  maybeUpdateQueueSizeStat(pausable_ack_queue_.size());
}

// Checks whether external conditions allow sending a DeltaDiscoveryRequest. (Does not check
// whether we *want* to send a DeltaDiscoveryRequest).
bool GrpcMuxImpl::canSendDiscoveryRequest(const std::string& type_url) {
  if (pausable_ack_queue_.paused(type_url)) {
    ASSERT(false,
           fmt::format("canSendDiscoveryRequest() called on paused type_url {}. Pausedness is "
                       "supposed to be filtered out by whoWantsToSendDiscoveryRequest(). "
                       "Returning false, but your xDS might be about to get head-of-line blocked "
                       "- permanently, if the pause is never undone.",
                       type_url));
    return false;
  } else if (!grpcStreamAvailable()) {
    ENVOY_LOG(trace, "No stream available to send a discovery request for {}.", type_url);
    return false;
  } else if (!rateLimitAllowsDrain()) {
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
absl::optional<std::string> GrpcMuxImpl::whoWantsToSendDiscoveryRequest() {
  // All ACKs are sent before plain updates. trySendDiscoveryRequests() relies on this. So, choose
  // type_url from pausable_ack_queue_ if possible, before looking at pending updates.
  if (!pausable_ack_queue_.empty()) {
    return pausable_ack_queue_.front().type_url_;
  }
  // If we're looking to send multiple non-ACK requests, send them in the order that their
  // subscriptions were initiated.
  for (const auto& sub_type : subscription_ordering_) {
    auto* sub = subscriptionStateFor(sub_type);
    if (sub->subscriptionUpdatePending() && !pausable_ack_queue_.paused(sub_type)) {
      return sub_type;
    }
  }
  return absl::nullopt;
}

} // namespace Config
} // namespace Envoy
