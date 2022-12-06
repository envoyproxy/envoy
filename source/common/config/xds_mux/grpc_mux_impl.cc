#include "source/common/config/xds_mux/grpc_mux_impl.h"

#include "envoy/service/discovery/v3/discovery.pb.h"

#include "source/common/common/assert.h"
#include "source/common/common/backoff_strategy.h"
#include "source/common/config/decoded_resource_impl.h"
#include "source/common/config/utility.h"
#include "source/common/config/xds_context_params.h"
#include "source/common/config/xds_resource.h"
#include "source/common/memory/utils.h"
#include "source/common/protobuf/protobuf.h"
#include "source/common/protobuf/utility.h"

namespace Envoy {
namespace Config {
namespace XdsMux {

namespace {
class AllMuxesState {
public:
  void insert(ShutdownableMux* mux) { muxes_.insert(mux); }

  void erase(ShutdownableMux* mux) { muxes_.erase(mux); }

  void shutdownAll() {
    for (auto& mux : muxes_) {
      mux->shutdown();
    }
  }

private:
  absl::flat_hash_set<ShutdownableMux*> muxes_;
};
using AllMuxes = ThreadSafeSingleton<AllMuxesState>;
} // namespace

template <class S, class F, class RQ, class RS>
GrpcMuxImpl<S, F, RQ, RS>::GrpcMuxImpl(
    std::unique_ptr<F> subscription_state_factory, bool skip_subsequent_node,
    const LocalInfo::LocalInfo& local_info, Grpc::RawAsyncClientPtr&& async_client,
    Event::Dispatcher& dispatcher, const Protobuf::MethodDescriptor& service_method,
    Random::RandomGenerator& random, Stats::Scope& scope,
    const RateLimitSettings& rate_limit_settings, CustomConfigValidatorsPtr&& config_validators,
    XdsResourcesDelegateOptRef xds_resources_delegate, const std::string& target_xds_authority)
    : grpc_stream_(this, std::move(async_client), service_method, random, dispatcher, scope,
                   rate_limit_settings),
      subscription_state_factory_(std::move(subscription_state_factory)),
      skip_subsequent_node_(skip_subsequent_node), local_info_(local_info),
      dynamic_update_callback_handle_(local_info.contextProvider().addDynamicContextUpdateCallback(
          [this](absl::string_view resource_type_url) {
            onDynamicContextUpdate(resource_type_url);
          })),
      config_validators_(std::move(config_validators)),
      xds_resources_delegate_(xds_resources_delegate), target_xds_authority_(target_xds_authority) {
  Config::Utility::checkLocalInfo("ads", local_info);
  AllMuxes::get().insert(this);
}

template <class S, class F, class RQ, class RS> GrpcMuxImpl<S, F, RQ, RS>::~GrpcMuxImpl() {
  AllMuxes::get().erase(this);
}

template <class S, class F, class RQ, class RS> void GrpcMuxImpl<S, F, RQ, RS>::shutdownAll() {
  AllMuxes::get().shutdownAll();
}

template <class S, class F, class RQ, class RS>
void GrpcMuxImpl<S, F, RQ, RS>::onDynamicContextUpdate(absl::string_view resource_type_url) {
  ENVOY_LOG(debug, "GrpcMuxImpl::onDynamicContextUpdate for {}", resource_type_url);
  auto sub = subscriptions_.find(resource_type_url);
  if (sub == subscriptions_.end()) {
    return;
  }
  sub->second->setDynamicContextChanged();
  trySendDiscoveryRequests();
}

template <class S, class F, class RQ, class RS>
Config::GrpcMuxWatchPtr GrpcMuxImpl<S, F, RQ, RS>::addWatch(
    const std::string& type_url, const absl::flat_hash_set<std::string>& resources,
    SubscriptionCallbacks& callbacks, OpaqueResourceDecoderSharedPtr resource_decoder,
    const SubscriptionOptions& options) {
  auto watch_map = watch_maps_.find(type_url);
  if (watch_map == watch_maps_.end()) {
    // We don't yet have a subscription for type_url! Make one!
    watch_map =
        watch_maps_
            .emplace(type_url, std::make_unique<WatchMap>(options.use_namespace_matching_, type_url,
                                                          *config_validators_.get()))
            .first;
    subscriptions_.emplace(type_url, subscription_state_factory_->makeSubscriptionState(
                                         type_url, *watch_maps_[type_url], resource_decoder,
                                         xds_resources_delegate_, target_xds_authority_));
    subscription_ordering_.emplace_back(type_url);
  }

  Watch* watch = watch_map->second->addWatch(callbacks, *resource_decoder);
  // updateWatch() queues a discovery request if any of 'resources' are not yet subscribed.
  updateWatch(type_url, watch, resources, options);
  return std::make_unique<WatchImpl>(type_url, watch, *this, options);
}

// Updates the list of resource names watched by the given watch. If an added name is new across
// the whole subscription, or if a removed name has no other watch interested in it, then the
// subscription will enqueue and attempt to send an appropriate discovery request.
template <class S, class F, class RQ, class RS>
void GrpcMuxImpl<S, F, RQ, RS>::updateWatch(const std::string& type_url, Watch* watch,
                                            const absl::flat_hash_set<std::string>& resources,
                                            const SubscriptionOptions& options) {
  ENVOY_LOG(debug, "GrpcMuxImpl::updateWatch for {}", type_url);
  ASSERT(watch != nullptr);
  auto& sub = subscriptionStateFor(type_url);
  WatchMap& watch_map = watchMapFor(type_url);

  // We need to prepare xdstp:// resources for the transport, by normalizing and adding any extra
  // context parameters.
  absl::flat_hash_set<std::string> effective_resources;
  for (const auto& resource : resources) {
    if (XdsResourceIdentifier::hasXdsTpScheme(resource)) {
      auto xdstp_resource = XdsResourceIdentifier::decodeUrn(resource);
      if (options.add_xdstp_node_context_params_) {
        const auto context = XdsContextParams::encodeResource(
            local_info_.contextProvider().nodeContext(), xdstp_resource.context(), {}, {});
        xdstp_resource.mutable_context()->CopyFrom(context);
      }
      XdsResourceIdentifier::EncodeOptions encode_options;
      encode_options.sort_context_params_ = true;
      effective_resources.insert(XdsResourceIdentifier::encodeUrn(xdstp_resource, encode_options));
    } else {
      effective_resources.insert(resource);
    }
  }

  auto added_removed = watch_map.updateWatchInterest(watch, effective_resources);
  if (options.use_namespace_matching_) {
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

template <class S, class F, class RQ, class RS>
void GrpcMuxImpl<S, F, RQ, RS>::removeWatch(const std::string& type_url, Watch* watch) {
  updateWatch(type_url, watch, {}, {});
  watchMapFor(type_url).removeWatch(watch);
}

template <class S, class F, class RQ, class RS>
ScopedResume GrpcMuxImpl<S, F, RQ, RS>::pause(const std::string& type_url) {
  return pause(std::vector<std::string>{type_url});
}

template <class S, class F, class RQ, class RS>
ScopedResume GrpcMuxImpl<S, F, RQ, RS>::pause(const std::vector<std::string> type_urls) {
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

template <class S, class F, class RQ, class RS>
void GrpcMuxImpl<S, F, RQ, RS>::sendGrpcMessage(RQ& msg_proto, S& sub_state) {
  if (sub_state.dynamicContextChanged() || !anyRequestSentYetInCurrentStream() ||
      !skipSubsequentNode()) {
    msg_proto.mutable_node()->CopyFrom(localInfo().node());
  }
  sendMessage(msg_proto);
  setAnyRequestSentYetInCurrentStream(true);
  sub_state.clearDynamicContextChanged();
}

template <class S, class F, class RQ, class RS>
void GrpcMuxImpl<S, F, RQ, RS>::genericHandleResponse(const std::string& type_url,
                                                      const RS& response_proto,
                                                      ControlPlaneStats& control_plane_stats) {
  auto sub = subscriptions_.find(type_url);
  if (sub == subscriptions_.end()) {
    ENVOY_LOG(warn,
              "The server sent an xDS response proto with type_url {}, which we have "
              "not subscribed to. Ignoring.",
              type_url);
    return;
  }

  if (response_proto.has_control_plane()) {
    control_plane_stats.identifier_.set(response_proto.control_plane().identifier());

    if (response_proto.control_plane().identifier() != sub->second->controlPlaneIdentifier()) {
      sub->second->setControlPlaneIdentifier(response_proto.control_plane().identifier());
      ENVOY_LOG(debug, "Receiving gRPC updates for {} from {}", response_proto.type_url(),
                sub->second->controlPlaneIdentifier());
    }
  }

  pausable_ack_queue_.push(sub->second->handleResponse(response_proto));
  trySendDiscoveryRequests();
  Memory::Utils::tryShrinkHeap();
}

template <class S, class F, class RQ, class RS> void GrpcMuxImpl<S, F, RQ, RS>::start() {
  ENVOY_LOG(debug, "GrpcMuxImpl now trying to establish a stream");
  grpc_stream_.establishNewStream();
}

template <class S, class F, class RQ, class RS>
void GrpcMuxImpl<S, F, RQ, RS>::handleEstablishedStream() {
  ENVOY_LOG(debug, "GrpcMuxImpl stream successfully established");
  for (auto& [type_url, subscription_state] : subscriptions_) {
    subscription_state->markStreamFresh();
  }
  setAnyRequestSentYetInCurrentStream(false);
  maybeUpdateQueueSizeStat(0);
  pausable_ack_queue_.clear();
  trySendDiscoveryRequests();
}

template <class S, class F, class RQ, class RS>
void GrpcMuxImpl<S, F, RQ, RS>::handleStreamEstablishmentFailure() {
  ENVOY_LOG(debug, "GrpcMuxImpl stream failed to establish");
  // If this happens while Envoy is still initializing, the onConfigUpdateFailed() we ultimately
  // call on CDS will cause LDS to start up, which adds to subscriptions_ here. So, to avoid a
  // crash, the iteration needs to dance around a little: collect pointers to all
  // SubscriptionStates, call on all those pointers we haven't yet called on, repeat if there are
  // now more SubscriptionStates.
  absl::flat_hash_map<std::string, S*> all_subscribed;
  absl::flat_hash_map<std::string, S*> already_called;
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

template <class S, class F, class RQ, class RS>
S& GrpcMuxImpl<S, F, RQ, RS>::subscriptionStateFor(const std::string& type_url) {
  auto sub = subscriptions_.find(type_url);
  RELEASE_ASSERT(sub != subscriptions_.end(),
                 fmt::format("Tried to look up SubscriptionState for non-existent subscription {}.",
                             type_url));
  return *sub->second;
}

template <class S, class F, class RQ, class RS>
WatchMap& GrpcMuxImpl<S, F, RQ, RS>::watchMapFor(const std::string& type_url) {
  auto watch_map = watch_maps_.find(type_url);
  RELEASE_ASSERT(
      watch_map != watch_maps_.end(),
      fmt::format("Tried to look up WatchMap for non-existent subscription {}.", type_url));
  return *watch_map->second;
}

template <class S, class F, class RQ, class RS>
void GrpcMuxImpl<S, F, RQ, RS>::trySendDiscoveryRequests() {
  if (shutdown_) {
    return;
  }

  while (true) {
    // Do any of our subscriptions even want to send a request?
    absl::optional<std::string> request_type_if_any = whoWantsToSendDiscoveryRequest();
    if (!request_type_if_any.has_value()) {
      break;
    }
    // If so, which one (by type_url)?
    std::string next_request_type_url = request_type_if_any.value();
    auto& sub = subscriptionStateFor(next_request_type_url);
    ENVOY_LOG(debug, "GrpcMuxImpl wants to send discovery request for {}", next_request_type_url);
    // Try again later if paused/rate limited/stream down.
    if (!canSendDiscoveryRequest(next_request_type_url)) {
      break;
    }
    std::unique_ptr<RQ> request;
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
    ENVOY_LOG(debug, "GrpcMuxImpl skip_subsequent_node: {}", skipSubsequentNode());
    sendGrpcMessage(*request, sub);
  }
  maybeUpdateQueueSizeStat(pausable_ack_queue_.size());
}

// Checks whether external conditions allow sending a discovery request. (Does not check
// whether we *want* to send a discovery request).
template <class S, class F, class RQ, class RS>
bool GrpcMuxImpl<S, F, RQ, RS>::canSendDiscoveryRequest(const std::string& type_url) {
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
template <class S, class F, class RQ, class RS>
absl::optional<std::string> GrpcMuxImpl<S, F, RQ, RS>::whoWantsToSendDiscoveryRequest() {
  // All ACKs are sent before plain updates. trySendDiscoveryRequests() relies on this. So, choose
  // type_url from pausable_ack_queue_ if possible, before looking at pending updates.
  if (!pausable_ack_queue_.empty()) {
    return pausable_ack_queue_.front().type_url_;
  }
  // If we're looking to send multiple non-ACK requests, send them in the order that their
  // subscriptions were initiated.
  for (const auto& sub_type : subscription_ordering_) {
    auto& sub = subscriptionStateFor(sub_type);
    if (sub.subscriptionUpdatePending() && !pausable_ack_queue_.paused(sub_type)) {
      return sub_type;
    }
  }
  return absl::nullopt;
}

template class GrpcMuxImpl<DeltaSubscriptionState, DeltaSubscriptionStateFactory,
                           envoy::service::discovery::v3::DeltaDiscoveryRequest,
                           envoy::service::discovery::v3::DeltaDiscoveryResponse>;
template class GrpcMuxImpl<SotwSubscriptionState, SotwSubscriptionStateFactory,
                           envoy::service::discovery::v3::DiscoveryRequest,
                           envoy::service::discovery::v3::DiscoveryResponse>;

// Delta- and SotW-specific concrete subclasses:
GrpcMuxDelta::GrpcMuxDelta(Grpc::RawAsyncClientPtr&& async_client, Event::Dispatcher& dispatcher,
                           const Protobuf::MethodDescriptor& service_method,
                           Random::RandomGenerator& random, Stats::Scope& scope,
                           const RateLimitSettings& rate_limit_settings,
                           const LocalInfo::LocalInfo& local_info, bool skip_subsequent_node,
                           CustomConfigValidatorsPtr&& config_validators)
    : GrpcMuxImpl(std::make_unique<DeltaSubscriptionStateFactory>(dispatcher), skip_subsequent_node,
                  local_info, std::move(async_client), dispatcher, service_method, random, scope,
                  rate_limit_settings, std::move(config_validators)) {}

// GrpcStreamCallbacks for GrpcMuxDelta
void GrpcMuxDelta::requestOnDemandUpdate(const std::string& type_url,
                                         const absl::flat_hash_set<std::string>& for_update) {
  auto& sub = subscriptionStateFor(type_url);
  sub.updateSubscriptionInterest(for_update, {});
  // Tell the server about our change in interest, if any.
  if (sub.subscriptionUpdatePending()) {
    trySendDiscoveryRequests();
  }
}

GrpcMuxSotw::GrpcMuxSotw(Grpc::RawAsyncClientPtr&& async_client, Event::Dispatcher& dispatcher,
                         const Protobuf::MethodDescriptor& service_method,
                         Random::RandomGenerator& random, Stats::Scope& scope,
                         const RateLimitSettings& rate_limit_settings,
                         const LocalInfo::LocalInfo& local_info, bool skip_subsequent_node,
                         CustomConfigValidatorsPtr&& config_validators,
                         XdsResourcesDelegateOptRef xds_resources_delegate,
                         const std::string& target_xds_authority)
    : GrpcMuxImpl(std::make_unique<SotwSubscriptionStateFactory>(dispatcher), skip_subsequent_node,
                  local_info, std::move(async_client), dispatcher, service_method, random, scope,
                  rate_limit_settings, std::move(config_validators), xds_resources_delegate,
                  target_xds_authority) {}

Config::GrpcMuxWatchPtr NullGrpcMuxImpl::addWatch(const std::string&,
                                                  const absl::flat_hash_set<std::string>&,
                                                  SubscriptionCallbacks&,
                                                  OpaqueResourceDecoderSharedPtr,
                                                  const SubscriptionOptions&) {
  throw EnvoyException("ADS must be configured to support an ADS config source");
}

} // namespace XdsMux
} // namespace Config
} // namespace Envoy
