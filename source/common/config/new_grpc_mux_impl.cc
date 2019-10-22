#include "common/config/new_grpc_mux_impl.h"

#include "common/common/assert.h"
#include "common/common/backoff_strategy.h"
#include "common/common/token_bucket_impl.h"
#include "common/config/utility.h"
#include "common/protobuf/protobuf.h"
#include "common/protobuf/utility.h"

namespace Envoy {
namespace Config {

NewGrpcMuxImpl::NewGrpcMuxImpl(std::unique_ptr<SubscriptionStateFactory> subscription_state_factory,
                               bool skip_subsequent_node, const LocalInfo::LocalInfo& local_info)
    : subscription_state_factory_(std::move(subscription_state_factory)),
      skip_subsequent_node_(skip_subsequent_node), local_info_(local_info) {
  Config::Utility::checkLocalInfo("ads", local_info);
}

Watch* NewGrpcMuxImpl::addOrUpdateWatch(const std::string& type_url, Watch* watch,
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

void NewGrpcMuxImpl::removeWatch(const std::string& type_url, Watch* watch) {
  updateWatch(type_url, watch, {});
  watchMapFor(type_url).removeWatch(watch);
}

void NewGrpcMuxImpl::pause(const std::string& type_url) { pausable_ack_queue_.pause(type_url); }

void NewGrpcMuxImpl::resume(const std::string& type_url) {
  pausable_ack_queue_.resume(type_url);
  trySendDiscoveryRequests();
}

bool NewGrpcMuxImpl::paused(const std::string& type_url) const {
  return pausable_ack_queue_.paused(type_url);
}

void NewGrpcMuxImpl::genericHandleResponse(const std::string& type_url,
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

void NewGrpcMuxImpl::start() { establishGrpcStream(); }

void NewGrpcMuxImpl::handleEstablishedStream() {
  for (auto& sub : subscriptions_) {
    sub.second->markStreamFresh();
  }
  set_any_request_sent_yet_in_current_stream(false);
  trySendDiscoveryRequests();
}

void NewGrpcMuxImpl::disableInitFetchTimeoutTimer() {
  for (auto& sub : subscriptions_) {
    sub.second->disableInitFetchTimeoutTimer();
  }
}

void NewGrpcMuxImpl::handleStreamEstablishmentFailure() {
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

Watch* NewGrpcMuxImpl::addWatch(const std::string& type_url, const std::set<std::string>& resources,
                                SubscriptionCallbacks& callbacks,
                                std::chrono::milliseconds init_fetch_timeout) {
  auto watch_map = watch_maps_.find(type_url);
  if (watch_map == watch_maps_.end()) {
    // We don't yet have a subscription for type_url! Make one!
    watch_map = watch_maps_.emplace(type_url, std::make_unique<WatchMap>()).first;
    subscriptions_.emplace(type_url, subscription_state_factory_->makeSubscriptionState(
                                         type_url, *watch_maps_[type_url], init_fetch_timeout));
    subscription_ordering_.emplace_back(type_url);
  }

  Watch* watch = watch_map->second->addWatch(callbacks);
  // updateWatch() queues a discovery request if any of 'resources' are not yet subscribed.
  updateWatch(type_url, watch, resources);
  return watch;
}

// Updates the list of resource names watched by the given watch. If an added name is new across
// the whole subscription, or if a removed name has no other watch interested in it, then the
// subscription will enqueue and attempt to send an appropriate discovery request.
void NewGrpcMuxImpl::updateWatch(const std::string& type_url, Watch* watch,
                                 const std::set<std::string>& resources) {
  ASSERT(watch != nullptr);
  SubscriptionState& sub = subscriptionStateFor(type_url);
  WatchMap& watch_map = watchMapFor(type_url);

  auto added_removed = watch_map.updateWatchInterest(watch, resources);
  sub.updateSubscriptionInterest(added_removed.added_, added_removed.removed_);

  // Tell the server about our change in interest, if any.
  if (sub.subscriptionUpdatePending()) {
    trySendDiscoveryRequests();
  }
}

SubscriptionState& NewGrpcMuxImpl::subscriptionStateFor(const std::string& type_url) {
  auto sub = subscriptions_.find(type_url);
  RELEASE_ASSERT(sub != subscriptions_.end(),
                 fmt::format("Tried to look up SubscriptionState for non-existent subscription {}.",
                             type_url));
  return *sub->second;
}

WatchMap& NewGrpcMuxImpl::watchMapFor(const std::string& type_url) {
  auto watch_map = watch_maps_.find(type_url);
  RELEASE_ASSERT(
      watch_map != watch_maps_.end(),
      fmt::format("Tried to look up WatchMap for non-existent subscription {}.", type_url));
  return *watch_map->second;
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
    SubscriptionState& sub = subscriptionStateFor(next_request_type_url);
    // Try again later if paused/rate limited/stream down.
    if (!canSendDiscoveryRequest(next_request_type_url)) {
      break;
    }
    // Get our subscription state to generate the appropriate DeltaDiscoveryRequest, and send.
    if (!pausable_ack_queue_.empty()) {
      // Because ACKs take precedence over plain requests, if there is anything in the queue, it's
      // safe to assume it's of the type_url that we're wanting to send.
      //
      // getNextRequestWithAck() returns a raw unowned pointer, which sendGrpcMessage deletes.
      sendGrpcMessage(sub.getNextRequestWithAck(pausable_ack_queue_.front()));
      pausable_ack_queue_.pop();
    } else {
      // getNextRequestAckless() returns a raw unowned pointer, which sendGrpcMessage deletes.
      sendGrpcMessage(sub.getNextRequestAckless());
    }
  }
  maybeUpdateQueueSizeStat(pausable_ack_queue_.size());
}

// Checks whether external conditions allow sending a DeltaDiscoveryRequest. (Does not check
// whether we *want* to send a DeltaDiscoveryRequest).
bool NewGrpcMuxImpl::canSendDiscoveryRequest(const std::string& type_url) {
  RELEASE_ASSERT(
      !pausable_ack_queue_.paused(type_url),
      fmt::format("canSendDiscoveryRequest() called on paused type_url {}. Pausedness is "
                  "supposed to be filtered out by whoWantsToSendDiscoveryRequest(). ",
                  type_url));

  if (!grpcStreamAvailable()) {
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
absl::optional<std::string> NewGrpcMuxImpl::whoWantsToSendDiscoveryRequest() {
  // All ACKs are sent before plain updates. trySendDiscoveryRequests() relies on this. So, choose
  // type_url from pausable_ack_queue_ if possible, before looking at pending updates.
  if (!pausable_ack_queue_.empty()) {
    return pausable_ack_queue_.front().type_url_;
  }
  // If we're looking to send multiple non-ACK requests, send them in the order that their
  // subscriptions were initiated.
  for (const auto& sub_type : subscription_ordering_) {
    SubscriptionState& sub = subscriptionStateFor(sub_type);
    if (sub.subscriptionUpdatePending() && !pausable_ack_queue_.paused(sub_type)) {
      return sub_type;
    }
  }
  return absl::nullopt;
}

// Delta- and SotW-specific concrete subclasses:
GrpcMuxDelta::GrpcMuxDelta(Grpc::RawAsyncClientPtr&& async_client, Event::Dispatcher& dispatcher,
                           const Protobuf::MethodDescriptor& service_method,
                           Runtime::RandomGenerator& random, Stats::Scope& scope,
                           const RateLimitSettings& rate_limit_settings,
                           const LocalInfo::LocalInfo& local_info, bool skip_subsequent_node)
    : NewGrpcMuxImpl(std::make_unique<DeltaSubscriptionStateFactory>(dispatcher),
                     skip_subsequent_node, local_info),
      grpc_stream_(this, std::move(async_client), service_method, random, dispatcher, scope,
                   rate_limit_settings) {}

// GrpcStreamCallbacks for GrpcMuxDelta
void GrpcMuxDelta::onStreamEstablished() { handleEstablishedStream(); }
void GrpcMuxDelta::onEstablishmentFailure() { handleStreamEstablishmentFailure(); }
void GrpcMuxDelta::onWriteable() { trySendDiscoveryRequests(); }
void GrpcMuxDelta::onDiscoveryResponse(
    std::unique_ptr<envoy::api::v2::DeltaDiscoveryResponse>&& message) {
  genericHandleResponse(message->type_url(), message.get());
}

void GrpcMuxDelta::establishGrpcStream() { grpc_stream_.establishNewStream(); }
void GrpcMuxDelta::sendGrpcMessage(void* msg_proto_ptr) {
  std::unique_ptr<envoy::api::v2::DeltaDiscoveryRequest> typed_proto(
      static_cast<envoy::api::v2::DeltaDiscoveryRequest*>(msg_proto_ptr));
  if (!any_request_sent_yet_in_current_stream() || !skip_subsequent_node()) {
    typed_proto->mutable_node()->MergeFrom(local_info().node());
  }
  grpc_stream_.sendMessage(*typed_proto);
  set_any_request_sent_yet_in_current_stream(true);
}
void GrpcMuxDelta::maybeUpdateQueueSizeStat(uint64_t size) {
  grpc_stream_.maybeUpdateQueueSizeStat(size);
}
bool GrpcMuxDelta::grpcStreamAvailable() const { return grpc_stream_.grpcStreamAvailable(); }
bool GrpcMuxDelta::rateLimitAllowsDrain() { return grpc_stream_.checkRateLimitAllowsDrain(); }

GrpcMuxSotw::GrpcMuxSotw(Grpc::RawAsyncClientPtr&& async_client, Event::Dispatcher& dispatcher,
                         const Protobuf::MethodDescriptor& service_method,
                         Runtime::RandomGenerator& random, Stats::Scope& scope,
                         const RateLimitSettings& rate_limit_settings,
                         const LocalInfo::LocalInfo& local_info, bool skip_subsequent_node)
    : NewGrpcMuxImpl(std::make_unique<SotwSubscriptionStateFactory>(dispatcher),
                     skip_subsequent_node, local_info),
      grpc_stream_(this, std::move(async_client), service_method, random, dispatcher, scope,
                   rate_limit_settings) {}

// GrpcStreamCallbacks for GrpcMuxSotw
void GrpcMuxSotw::onStreamEstablished() { handleEstablishedStream(); }
void GrpcMuxSotw::onEstablishmentFailure() { handleStreamEstablishmentFailure(); }
void GrpcMuxSotw::onWriteable() { trySendDiscoveryRequests(); }
void GrpcMuxSotw::onDiscoveryResponse(
    std::unique_ptr<envoy::api::v2::DiscoveryResponse>&& message) {
  genericHandleResponse(message->type_url(), message.get());
}

void GrpcMuxSotw::establishGrpcStream() { grpc_stream_.establishNewStream(); }

void GrpcMuxSotw::sendGrpcMessage(void* msg_proto_ptr) {
  std::unique_ptr<envoy::api::v2::DiscoveryRequest> typed_proto(
      static_cast<envoy::api::v2::DiscoveryRequest*>(msg_proto_ptr));
  if (!any_request_sent_yet_in_current_stream() || !skip_subsequent_node()) {
    typed_proto->mutable_node()->MergeFrom(local_info().node());
  }
  grpc_stream_.sendMessage(*typed_proto);
  set_any_request_sent_yet_in_current_stream(true);
}

void GrpcMuxSotw::maybeUpdateQueueSizeStat(uint64_t size) {
  grpc_stream_.maybeUpdateQueueSizeStat(size);
}

bool GrpcMuxSotw::grpcStreamAvailable() const { return grpc_stream_.grpcStreamAvailable(); }
bool GrpcMuxSotw::rateLimitAllowsDrain() { return grpc_stream_.checkRateLimitAllowsDrain(); }

} // namespace Config
} // namespace Envoy
