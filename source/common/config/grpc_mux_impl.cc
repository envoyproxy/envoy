#include "common/config/grpc_mux_impl.h"

#include "envoy/service/discovery/v3/discovery.pb.h"

#include "common/common/assert.h"
#include "common/common/backoff_strategy.h"
#include "common/config/decoded_resource_impl.h"
#include "common/config/utility.h"
#include "common/config/version_converter.h"
#include "common/memory/utils.h"
#include "common/protobuf/protobuf.h"
#include "common/protobuf/utility.h"

namespace Envoy {
namespace Config {

GrpcMuxImpl::GrpcMuxImpl(std::unique_ptr<SubscriptionStateFactory> subscription_state_factory,
                         bool skip_subsequent_node, const LocalInfo::LocalInfo& local_info,
                         envoy::config::core::v3::ApiVersion transport_api_version)
    : subscription_state_factory_(std::move(subscription_state_factory)),
      skip_subsequent_node_(skip_subsequent_node), local_info_(local_info),
      transport_api_version_(transport_api_version),
      enable_type_url_downgrade_and_upgrade_(Runtime::runtimeFeatureEnabled(
          "envoy.reloadable_features.enable_type_url_downgrade_and_upgrade")) {
  Config::Utility::checkLocalInfo("ads", local_info);
}

Watch* GrpcMuxImpl::addWatch(const std::string& type_url, const std::set<std::string>& resources,
                             SubscriptionCallbacks& callbacks,
                             OpaqueResourceDecoder& resource_decoder,
                             std::chrono::milliseconds init_fetch_timeout,
                             const bool use_namespace_matching) {
  auto watch_map = watch_maps_.find(type_url);
  if (watch_map == watch_maps_.end()) {
    // We don't yet have a subscription for type_url! Make one!
    if (enable_type_url_downgrade_and_upgrade_) {
      registerVersionedTypeUrl(type_url);
    }
    watch_map =
        watch_maps_.emplace(type_url, std::make_unique<WatchMap>(use_namespace_matching)).first;
    subscriptions_.emplace(type_url, subscription_state_factory_->makeSubscriptionState(
                                         type_url, *watch_maps_[type_url], init_fetch_timeout));
    subscription_ordering_.emplace_back(type_url);
  }

  Watch* watch = watch_map->second->addWatch(callbacks, resource_decoder);
  // updateWatch() queues a discovery request if any of 'resources' are not yet subscribed.
  updateWatch(type_url, watch, resources, use_namespace_matching);
  return watch;
}

// Updates the list of resource names watched by the given watch. If an added name is new across
// the whole subscription, or if a removed name has no other watch interested in it, then the
// subscription will enqueue and attempt to send an appropriate discovery request.
void GrpcMuxImpl::updateWatch(const std::string& type_url, Watch* watch,
                              const std::set<std::string>& resources,
                              const bool creating_namespace_watch) {
  ENVOY_LOG(debug, "GrpcMuxImpl::updateWatch for {}", type_url);
  ASSERT(watch != nullptr);
  SubscriptionState& sub = subscriptionStateFor(type_url);
  WatchMap& watch_map = watchMapFor(type_url);

  auto added_removed = watch_map.updateWatchInterest(watch, resources);
  if (creating_namespace_watch) {
    // This is to prevent sending out of requests that contain prefixes instead of resource names
    sub.updateSubscriptionInterest({}, {});
  } else {
    sub.updateSubscriptionInterest(added_removed.added_, added_removed.removed_);
  }

  // Tell the server about our change in interest, if any.
  if (sub.subscriptionUpdatePending()) {
    trySendDiscoveryRequests();
  }
}

void GrpcMuxImpl::removeWatch(const std::string& type_url, Watch* watch) {
  updateWatch(type_url, watch, {});
  watchMapFor(type_url).removeWatch(watch);
}

ScopedResume GrpcMuxImpl::pause(const std::string& type_url) {
  return pause(std::vector<std::string>{type_url});
}

ScopedResume GrpcMuxImpl::pause(const std::vector<std::string> type_urls) {
  for (const auto& type_url : type_urls) {
    pausable_ack_queue_.pause(type_url);
  }

  return std::make_unique<Cleanup>([this, type_urls]() {
    for (const auto& type_url : type_urls) {
      pausable_ack_queue_.resume(type_url);
      trySendDiscoveryRequests();
    }
  });
}

bool GrpcMuxImpl::paused(const std::string& type_url) const {
  return pausable_ack_queue_.paused(type_url);
}

void GrpcMuxImpl::registerVersionedTypeUrl(const std::string& type_url) {
  TypeUrlMap& type_url_map = typeUrlMap();
  if (type_url_map.find(type_url) != type_url_map.end()) {
    return;
  }
  // If type_url is v3, earlier_type_url will contain v2 type url.
  absl::optional<std::string> earlier_type_url = ApiTypeOracle::getEarlierTypeUrl(type_url);
  // Register v2 to v3 and v3 to v2 type_url mapping in the hash map.
  if (earlier_type_url.has_value()) {
    type_url_map[earlier_type_url.value()] = type_url;
    type_url_map[type_url] = earlier_type_url.value();
  }
}

void GrpcMuxImpl::genericHandleResponse(const std::string& type_url,
                                        const void* response_proto_ptr) {
  auto sub = subscriptions_.find(type_url);
  // If this type url is not watched, try another version type url.
  if (enable_type_url_downgrade_and_upgrade_ && sub == subscriptions_.end()) {
    registerVersionedTypeUrl(type_url);
    TypeUrlMap& type_url_map = typeUrlMap();
    if (type_url_map.find(type_url) != type_url_map.end()) {
      sub = subscriptions_.find(type_url_map[type_url]);
    }
  }
  if (sub == subscriptions_.end()) {
    ENVOY_LOG(warn,
              "The server sent an xDS response proto with type_url {}, which we have "
              "not subscribed to. Ignoring.",
              type_url);
    return;
  }
  pausable_ack_queue_.push(sub->second->handleResponse(response_proto_ptr));
  trySendDiscoveryRequests();
  Memory::Utils::tryShrinkHeap();
}

void GrpcMuxImpl::start() {
  ENVOY_LOG(debug, "GrpcMuxImpl now trying to establish a stream");
  establishGrpcStream();
}

void GrpcMuxImpl::handleEstablishedStream() {
  ENVOY_LOG(debug, "GrpcMuxImpl stream successfully established");
  for (auto& [type_url, subscription_state] : subscriptions_) {
    subscription_state->markStreamFresh();
  }
  set_any_request_sent_yet_in_current_stream(false);
  maybeUpdateQueueSizeStat(0);
  pausable_ack_queue_.clear();
  trySendDiscoveryRequests();
}

void GrpcMuxImpl::disableInitFetchTimeoutTimer() {
  for (auto& [type_url, subscription_state] : subscriptions_) {
    subscription_state->disableInitFetchTimeoutTimer();
  }
}

void GrpcMuxImpl::handleStreamEstablishmentFailure() {
  ENVOY_LOG(debug, "GrpcMuxImpl stream failed to establish");
  // If this happens while Envoy is still initializing, the onConfigUpdateFailed() we ultimately
  // call on CDS will cause LDS to start up, which adds to subscriptions_ here. So, to avoid a
  // crash, the iteration needs to dance around a little: collect pointers to all
  // SubscriptionStates, call on all those pointers we haven't yet called on, repeat if there are
  // now more SubscriptionStates.
  absl::flat_hash_map<std::string, SubscriptionState*> all_subscribed;
  absl::flat_hash_map<std::string, SubscriptionState*> already_called;
  do {
    for (auto& [type_url, subscription_state] : subscriptions_) {
      all_subscribed[type_url] = subscription_state.get();
    }
    for (auto& sub : all_subscribed) {
      if (already_called.insert(sub).second) { // insert succeeded ==> not already called
        sub.second->handleEstablishmentFailure();
      }
    }
  } while (all_subscribed.size() != subscriptions_.size());
}

SubscriptionState& GrpcMuxImpl::subscriptionStateFor(const std::string& type_url) {
  auto sub = subscriptions_.find(type_url);
  RELEASE_ASSERT(sub != subscriptions_.end(),
                 fmt::format("Tried to look up SubscriptionState for non-existent subscription {}.",
                             type_url));
  return *sub->second;
}

WatchMap& GrpcMuxImpl::watchMapFor(const std::string& type_url) {
  auto watch_map = watch_maps_.find(type_url);
  RELEASE_ASSERT(
      watch_map != watch_maps_.end(),
      fmt::format("Tried to look up WatchMap for non-existent subscription {}.", type_url));
  return *watch_map->second;
}

void GrpcMuxImpl::trySendDiscoveryRequests() {
  while (true) {
    // Do any of our subscriptions even want to send a request?
    absl::optional<std::string> request_type_if_any = whoWantsToSendDiscoveryRequest();
    if (!request_type_if_any.has_value()) {
      break;
    }
    // If so, which one (by type_url)?
    std::string next_request_type_url = request_type_if_any.value();
    SubscriptionState& sub = subscriptionStateFor(next_request_type_url);
    ENVOY_LOG(debug, "GrpcMuxImpl wants to send discovery request for {}", next_request_type_url);
    // Try again later if paused/rate limited/stream down.
    if (!canSendDiscoveryRequest(next_request_type_url)) {
      break;
    }
    void* request;
    // Get our subscription state to generate the appropriate discovery request, and send.
    if (!pausable_ack_queue_.empty()) {
      // Because ACKs take precedence over plain requests, if there is anything in the queue, it's
      // safe to assume it's of the type_url that we're wanting to send.
      //
      // getNextRequestWithAck() returns a raw unowned pointer, which sendGrpcMessage deletes.
      request = sub.getNextRequestWithAck(pausable_ack_queue_.popFront());
      ENVOY_LOG(debug, "GrpcMuxImpl sent ACK discovery request for {}", next_request_type_url);
    } else {
      // Returns a raw unowned pointer, which sendGrpcMessage deletes.
      request = sub.getNextRequestAckless();
      ENVOY_LOG(debug, "GrpcMuxImpl sent non-ACK discovery request for {}", next_request_type_url);
    }
    ENVOY_LOG(debug, "GrpcMuxImpl skip_subsequent_node: {}", skip_subsequent_node());
    sendGrpcMessage(request);
  }
  maybeUpdateQueueSizeStat(pausable_ack_queue_.size());
}

// Checks whether external conditions allow sending a discovery request. (Does not check
// whether we *want* to send a discovery request).
bool GrpcMuxImpl::canSendDiscoveryRequest(const std::string& type_url) {
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

// Checks whether we have something to say in a discovery request, which can be an ACK and/or
// a subscription update. (Does not check whether we *can* send that discovery request).
// Returns the type_url we should send the discovery request for (if any).
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
                           envoy::config::core::v3::ApiVersion transport_api_version,
                           Random::RandomGenerator& random, Stats::Scope& scope,
                           const RateLimitSettings& rate_limit_settings,
                           const LocalInfo::LocalInfo& local_info, bool skip_subsequent_node)
    : GrpcMuxImpl(std::make_unique<DeltaSubscriptionStateFactory>(dispatcher), skip_subsequent_node,
                  local_info, transport_api_version),
      grpc_stream_(this, std::move(async_client), service_method, random, dispatcher, scope,
                   rate_limit_settings) {}

// GrpcStreamCallbacks for GrpcMuxDelta
void GrpcMuxDelta::onStreamEstablished() { handleEstablishedStream(); }
void GrpcMuxDelta::onEstablishmentFailure() { handleStreamEstablishmentFailure(); }
void GrpcMuxDelta::onWriteable() { trySendDiscoveryRequests(); }
void GrpcMuxDelta::onDiscoveryResponse(
    std::unique_ptr<envoy::service::discovery::v3::DeltaDiscoveryResponse>&& message,
    ControlPlaneStats&) {
  genericHandleResponse(message->type_url(), message.get());
}

void GrpcMuxDelta::establishGrpcStream() { grpc_stream_.establishNewStream(); }
void GrpcMuxDelta::sendGrpcMessage(void* msg_proto_ptr) {
  std::unique_ptr<envoy::service::discovery::v3::DeltaDiscoveryRequest> typed_proto(
      static_cast<envoy::service::discovery::v3::DeltaDiscoveryRequest*>(msg_proto_ptr));
  if (!any_request_sent_yet_in_current_stream() || !skip_subsequent_node()) {
    typed_proto->mutable_node()->MergeFrom(local_info().node());
  }
  VersionConverter::prepareMessageForGrpcWire(*typed_proto, transport_api_version());
  grpc_stream_.sendMessage(*typed_proto);
  set_any_request_sent_yet_in_current_stream(true);
}
void GrpcMuxDelta::maybeUpdateQueueSizeStat(uint64_t size) {
  grpc_stream_.maybeUpdateQueueSizeStat(size);
}
bool GrpcMuxDelta::grpcStreamAvailable() const { return grpc_stream_.grpcStreamAvailable(); }
bool GrpcMuxDelta::rateLimitAllowsDrain() { return grpc_stream_.checkRateLimitAllowsDrain(); }

void GrpcMuxDelta::requestOnDemandUpdate(const std::string& type_url,
                                         const std::set<std::string>& for_update) {
  SubscriptionState& sub = subscriptionStateFor(type_url);
  sub.updateSubscriptionInterest(for_update, {});
  // Tell the server about our change in interest, if any.
  if (sub.subscriptionUpdatePending()) {
    trySendDiscoveryRequests();
  }
}

GrpcMuxSotw::GrpcMuxSotw(Grpc::RawAsyncClientPtr&& async_client, Event::Dispatcher& dispatcher,
                         const Protobuf::MethodDescriptor& service_method,
                         envoy::config::core::v3::ApiVersion transport_api_version,
                         Random::RandomGenerator& random, Stats::Scope& scope,
                         const RateLimitSettings& rate_limit_settings,
                         const LocalInfo::LocalInfo& local_info, bool skip_subsequent_node)
    : GrpcMuxImpl(std::make_unique<SotwSubscriptionStateFactory>(dispatcher), skip_subsequent_node,
                  local_info, transport_api_version),
      grpc_stream_(this, std::move(async_client), service_method, random, dispatcher, scope,
                   rate_limit_settings) {}

// GrpcStreamCallbacks for GrpcMuxSotw
void GrpcMuxSotw::onStreamEstablished() { handleEstablishedStream(); }
void GrpcMuxSotw::onEstablishmentFailure() { handleStreamEstablishmentFailure(); }
void GrpcMuxSotw::onWriteable() { trySendDiscoveryRequests(); }
void GrpcMuxSotw::onDiscoveryResponse(
    std::unique_ptr<envoy::service::discovery::v3::DiscoveryResponse>&& message,
    ControlPlaneStats& control_plane_stats) {
  if (message->has_control_plane()) {
    control_plane_stats.identifier_.set(message->control_plane().identifier());
  }
  genericHandleResponse(message->type_url(), message.get());
}

void GrpcMuxSotw::establishGrpcStream() { grpc_stream_.establishNewStream(); }

void GrpcMuxSotw::sendGrpcMessage(void* msg_proto_ptr) {
  std::unique_ptr<envoy::service::discovery::v3::DiscoveryRequest> typed_proto(
      static_cast<envoy::service::discovery::v3::DiscoveryRequest*>(msg_proto_ptr));
  if (!any_request_sent_yet_in_current_stream() || !skip_subsequent_node()) {
    typed_proto->mutable_node()->MergeFrom(local_info().node());
  }
  VersionConverter::prepareMessageForGrpcWire(*typed_proto, transport_api_version());
  grpc_stream_.sendMessage(*typed_proto);
  set_any_request_sent_yet_in_current_stream(true);
}

void GrpcMuxSotw::maybeUpdateQueueSizeStat(uint64_t size) {
  grpc_stream_.maybeUpdateQueueSizeStat(size);
}

bool GrpcMuxSotw::grpcStreamAvailable() const { return grpc_stream_.grpcStreamAvailable(); }
bool GrpcMuxSotw::rateLimitAllowsDrain() { return grpc_stream_.checkRateLimitAllowsDrain(); }

Watch* NullGrpcMuxImpl::addWatch(const std::string&, const std::set<std::string>&,
                                 SubscriptionCallbacks&, OpaqueResourceDecoder&,
                                 std::chrono::milliseconds, const bool) {
  throw EnvoyException("ADS must be configured to support an ADS config source");
}

void NullGrpcMuxImpl::updateWatch(const std::string&, Watch*, const std::set<std::string>&,
                                  const bool) {
  throw EnvoyException("ADS must be configured to support an ADS config source");
}

void NullGrpcMuxImpl::removeWatch(const std::string&, Watch*) {
  throw EnvoyException("ADS must be configured to support an ADS config source");
}

} // namespace Config
} // namespace Envoy
