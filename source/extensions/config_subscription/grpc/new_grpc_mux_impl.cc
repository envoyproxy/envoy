#include "source/extensions/config_subscription/grpc/new_grpc_mux_impl.h"

#include "envoy/service/discovery/v3/discovery.pb.h"

#include "source/common/common/assert.h"
#include "source/common/common/backoff_strategy.h"
#include "source/common/common/token_bucket_impl.h"
#include "source/common/config/utility.h"
#include "source/common/config/xds_context_params.h"
#include "source/common/config/xds_resource.h"
#include "source/common/memory/utils.h"
#include "source/common/protobuf/protobuf.h"
#include "source/common/protobuf/utility.h"
#include "source/extensions/config_subscription/grpc/eds_resources_cache_impl.h"

namespace Envoy {
namespace Config {

namespace {
class AllMuxesState {
public:
  void insert(NewGrpcMuxImpl* mux) { muxes_.insert(mux); }

  void erase(NewGrpcMuxImpl* mux) { muxes_.erase(mux); }

  void shutdownAll() {
    for (auto& mux : muxes_) {
      mux->shutdown();
    }
  }

private:
  absl::flat_hash_set<NewGrpcMuxImpl*> muxes_;
};
using AllMuxes = ThreadSafeSingleton<AllMuxesState>;
} // namespace

NewGrpcMuxImpl::NewGrpcMuxImpl(GrpcMuxContext& grpc_mux_context)
    : dispatcher_(grpc_mux_context.dispatcher_),
      grpc_stream_(createGrpcStreamObject(std::move(grpc_mux_context.async_client_),
                                          std::move(grpc_mux_context.failover_async_client_),
                                          grpc_mux_context.service_method_, grpc_mux_context.scope_,
                                          std::move(grpc_mux_context.backoff_strategy_),
                                          grpc_mux_context.rate_limit_settings_)),
      local_info_(grpc_mux_context.local_info_),
      config_validators_(std::move(grpc_mux_context.config_validators_)),
      dynamic_update_callback_handle_(
          grpc_mux_context.local_info_.contextProvider().addDynamicContextUpdateCallback(
              [this](absl::string_view resource_type_url) {
                onDynamicContextUpdate(resource_type_url);
                return absl::OkStatus();
              })),
      xds_config_tracker_(grpc_mux_context.xds_config_tracker_),
      eds_resources_cache_(std::move(grpc_mux_context.eds_resources_cache_)) {
  AllMuxes::get().insert(this);
}

std::unique_ptr<GrpcStreamInterface<envoy::service::discovery::v3::DeltaDiscoveryRequest,
                                    envoy::service::discovery::v3::DeltaDiscoveryResponse>>
NewGrpcMuxImpl::createGrpcStreamObject(Grpc::RawAsyncClientPtr&& async_client,
                                       Grpc::RawAsyncClientPtr&& failover_async_client,
                                       const Protobuf::MethodDescriptor& service_method,
                                       Stats::Scope& scope, BackOffStrategyPtr&& backoff_strategy,
                                       const RateLimitSettings& rate_limit_settings) {
  if (Runtime::runtimeFeatureEnabled("envoy.restart_features.xds_failover_support")) {
    return std::make_unique<GrpcMuxFailover<envoy::service::discovery::v3::DeltaDiscoveryRequest,
                                            envoy::service::discovery::v3::DeltaDiscoveryResponse>>(
        /*primary_stream_creator=*/
        [&async_client, &service_method, &dispatcher = dispatcher_, &scope, &backoff_strategy,
         &rate_limit_settings](
            GrpcStreamCallbacks<envoy::service::discovery::v3::DeltaDiscoveryResponse>* callbacks)
            -> GrpcStreamInterfacePtr<envoy::service::discovery::v3::DeltaDiscoveryRequest,
                                      envoy::service::discovery::v3::DeltaDiscoveryResponse> {
          return std::make_unique<
              GrpcStream<envoy::service::discovery::v3::DeltaDiscoveryRequest,
                         envoy::service::discovery::v3::DeltaDiscoveryResponse>>(
              callbacks, std::move(async_client), service_method, dispatcher, scope,
              std::move(backoff_strategy), rate_limit_settings,
              GrpcStream<envoy::service::discovery::v3::DeltaDiscoveryRequest,
                         envoy::service::discovery::v3::DeltaDiscoveryResponse>::
                  ConnectedStateValue::FIRST_ENTRY);
        },
        /*failover_stream_creator=*/
        failover_async_client
            ? absl::make_optional(
                  [&failover_async_client, &service_method, &dispatcher = dispatcher_, &scope,
                   &rate_limit_settings](
                      GrpcStreamCallbacks<envoy::service::discovery::v3::DeltaDiscoveryResponse>*
                          callbacks)
                      -> GrpcStreamInterfacePtr<
                          envoy::service::discovery::v3::DeltaDiscoveryRequest,
                          envoy::service::discovery::v3::DeltaDiscoveryResponse> {
                    return std::make_unique<
                        GrpcStream<envoy::service::discovery::v3::DeltaDiscoveryRequest,
                                   envoy::service::discovery::v3::DeltaDiscoveryResponse>>(
                        callbacks, std::move(failover_async_client), service_method, dispatcher,
                        scope,
                        // TODO(adisuissa): the backoff strategy for the failover should
                        // be the same as the primary source.
                        std::make_unique<FixedBackOffStrategy>(
                            GrpcMuxFailover<envoy::service::discovery::v3::DeltaDiscoveryRequest,
                                            envoy::service::discovery::v3::DeltaDiscoveryResponse>::
                                DefaultFailoverBackoffMilliseconds),
                        rate_limit_settings,
                        GrpcStream<envoy::service::discovery::v3::DeltaDiscoveryRequest,
                                   envoy::service::discovery::v3::DeltaDiscoveryResponse>::
                            ConnectedStateValue::SECOND_ENTRY);
                  })
            : absl::nullopt,
        /*grpc_mux_callbacks=*/*this,
        /*dispatch=*/dispatcher_);
  }
  return std::make_unique<GrpcStream<envoy::service::discovery::v3::DeltaDiscoveryRequest,
                                     envoy::service::discovery::v3::DeltaDiscoveryResponse>>(
      this, std::move(async_client), service_method, dispatcher_, scope,
      std::move(backoff_strategy), rate_limit_settings,
      GrpcStream<
          envoy::service::discovery::v3::DeltaDiscoveryRequest,
          envoy::service::discovery::v3::DeltaDiscoveryResponse>::ConnectedStateValue::FIRST_ENTRY);
}

NewGrpcMuxImpl::~NewGrpcMuxImpl() { AllMuxes::get().erase(this); }

void NewGrpcMuxImpl::shutdownAll() { AllMuxes::get().shutdownAll(); }

void NewGrpcMuxImpl::onDynamicContextUpdate(absl::string_view resource_type_url) {
  auto sub = subscriptions_.find(resource_type_url);
  if (sub == subscriptions_.end()) {
    return;
  }
  sub->second->sub_state_.setMustSendDiscoveryRequest();
  trySendDiscoveryRequests();
}

ScopedResume NewGrpcMuxImpl::pause(const std::string& type_url) {
  return pause(std::vector<std::string>{type_url});
}

ScopedResume NewGrpcMuxImpl::pause(const std::vector<std::string> type_urls) {
  for (const auto& type_url : type_urls) {
    pausable_ack_queue_.pause(type_url);
  }

  return std::make_unique<Cleanup>([this, type_urls]() {
    for (const auto& type_url : type_urls) {
      pausable_ack_queue_.resume(type_url);
      if (!pausable_ack_queue_.paused(type_url)) {
        trySendDiscoveryRequests();
      }
    }
  });
}

void NewGrpcMuxImpl::onDiscoveryResponse(
    std::unique_ptr<envoy::service::discovery::v3::DeltaDiscoveryResponse>&& message,
    ControlPlaneStats& control_plane_stats) {
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

  if (message->has_control_plane()) {
    control_plane_stats.identifier_.set(message->control_plane().identifier());

    if (message->control_plane().identifier() != sub->second->control_plane_identifier_) {
      sub->second->control_plane_identifier_ = message->control_plane().identifier();
      ENVOY_LOG(debug, "Receiving gRPC updates for {} from {}", message->type_url(),
                sub->second->control_plane_identifier_);
    }
  }

  auto ack = sub->second->sub_state_.handleResponse(*message);

  // Processing point to record error if there is any failure after the response is processed.
  if (xds_config_tracker_.has_value() &&
      ack.error_detail_.code() != Grpc::Status::WellKnownGrpcStatus::Ok) {
    xds_config_tracker_->onConfigRejected(*message, ack.error_detail_.message());
  }
  kickOffAck(ack);
  Memory::Utils::tryShrinkHeap();
}

void NewGrpcMuxImpl::onStreamEstablished() {
  for (auto& [type_url, subscription] : subscriptions_) {
    UNREFERENCED_PARAMETER(type_url);
    subscription->sub_state_.markStreamFresh(should_send_initial_resource_versions_);
  }
  pausable_ack_queue_.clear();
  trySendDiscoveryRequests();
}

void NewGrpcMuxImpl::onEstablishmentFailure(bool next_attempt_may_send_initial_resource_version) {
  // If this happens while Envoy is still initializing, the onConfigUpdateFailed() we ultimately
  // call on CDS will cause LDS to start up, which adds to subscriptions_ here. So, to avoid a
  // crash, the iteration needs to dance around a little: collect pointers to all
  // SubscriptionStates, call on all those pointers we haven't yet called on, repeat if there are
  // now more SubscriptionStates.
  absl::flat_hash_map<std::string, DeltaSubscriptionState*> all_subscribed;
  absl::flat_hash_map<std::string, DeltaSubscriptionState*> already_called;
  do {
    for (auto& [type_url, subscription] : subscriptions_) {
      all_subscribed[type_url] = &subscription->sub_state_;
    }
    for (auto& sub : all_subscribed) {
      if (already_called.insert(sub).second) { // insert succeeded ==> not already called
        sub.second->handleEstablishmentFailure();
      }
    }
  } while (all_subscribed.size() != subscriptions_.size());
  should_send_initial_resource_versions_ = next_attempt_may_send_initial_resource_version;
}

void NewGrpcMuxImpl::onWriteable() { trySendDiscoveryRequests(); }

void NewGrpcMuxImpl::kickOffAck(UpdateAck ack) {
  pausable_ack_queue_.push(std::move(ack));
  trySendDiscoveryRequests();
}

// TODO(fredlas) to be removed from the GrpcMux interface very soon.
void NewGrpcMuxImpl::start() {
  ASSERT(!started_);
  if (started_) {
    return;
  }
  started_ = true;
  grpc_stream_->establishNewStream();
}

GrpcMuxWatchPtr NewGrpcMuxImpl::addWatch(const std::string& type_url,
                                         const absl::flat_hash_set<std::string>& resources,
                                         SubscriptionCallbacks& callbacks,
                                         OpaqueResourceDecoderSharedPtr resource_decoder,
                                         const SubscriptionOptions& options) {
  auto entry = subscriptions_.find(type_url);
  if (entry == subscriptions_.end()) {
    // We don't yet have a subscription for type_url! Make one!
    addSubscription(type_url, options.use_namespace_matching_);
    return addWatch(type_url, resources, callbacks, resource_decoder, options);
  }

  Watch* watch = entry->second->watch_map_.addWatch(callbacks, *resource_decoder);
  // updateWatch() queues a discovery request if any of 'resources' are not yet subscribed.
  updateWatch(type_url, watch, resources, options);
  return std::make_unique<WatchImpl>(type_url, watch, *this, options);
}

absl::Status
NewGrpcMuxImpl::updateMuxSource(Grpc::RawAsyncClientPtr&& primary_async_client,
                                Grpc::RawAsyncClientPtr&& failover_async_client,
                                CustomConfigValidatorsPtr&& custom_config_validators,
                                Stats::Scope& scope, BackOffStrategyPtr&& backoff_strategy,
                                const envoy::config::core::v3::ApiConfigSource& ads_config_source) {
  // Process the rate limit settings.
  absl::StatusOr<RateLimitSettings> rate_limit_settings_or_error =
      Utility::parseRateLimitSettings(ads_config_source);
  RETURN_IF_NOT_OK_REF(rate_limit_settings_or_error.status());

  const Protobuf::MethodDescriptor& service_method =
      *Protobuf::DescriptorPool::generated_pool()->FindMethodByName(
          "envoy.service.discovery.v3.AggregatedDiscoveryService.DeltaAggregatedResources");

  // Disconnect from current xDS servers.
  ENVOY_LOG_MISC(info, "Replacing the xDS gRPC mux source");
  grpc_stream_->closeStream();
  grpc_stream_ = createGrpcStreamObject(std::move(primary_async_client),
                                        std::move(failover_async_client), service_method, scope,
                                        std::move(backoff_strategy), *rate_limit_settings_or_error);

  // Update the config validators.
  config_validators_ = std::move(custom_config_validators);
  // Update the watch map's config validators.
  for (auto& [type_url, subscription] : subscriptions_) {
    subscription->watch_map_.setConfigValidators(config_validators_.get());
  }

  // Start the subscriptions over the grpc_stream.
  grpc_stream_->establishNewStream();

  return absl::OkStatus();
}

// Updates the list of resource names watched by the given watch. If an added name is new across
// the whole subscription, or if a removed name has no other watch interested in it, then the
// subscription will enqueue and attempt to send an appropriate discovery request.
void NewGrpcMuxImpl::updateWatch(const std::string& type_url, Watch* watch,
                                 const absl::flat_hash_set<std::string>& resources,
                                 const SubscriptionOptions& options) {
  ASSERT(watch != nullptr);
  auto sub = subscriptions_.find(type_url);
  RELEASE_ASSERT(sub != subscriptions_.end(),
                 fmt::format("Watch of {} has no subscription to update.", type_url));
  // We need to prepare xdstp:// resources for the transport, by normalizing and adding any extra
  // context parameters.
  absl::flat_hash_set<std::string> effective_resources;
  for (const auto& resource : resources) {
    if (XdsResourceIdentifier::hasXdsTpScheme(resource)) {
      auto xdstp_resource_or_error = XdsResourceIdentifier::decodeUrn(resource);
      THROW_IF_NOT_OK_REF(xdstp_resource_or_error.status());
      auto xdstp_resource = xdstp_resource_or_error.value();
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
  auto added_removed = sub->second->watch_map_.updateWatchInterest(watch, effective_resources);
  if (options.use_namespace_matching_) {
    // This is to prevent sending out of requests that contain prefixes instead of resource names
    sub->second->sub_state_.updateSubscriptionInterest({}, {});
  } else {
    sub->second->sub_state_.updateSubscriptionInterest(added_removed.added_,
                                                       added_removed.removed_);
  }
  // Tell the server about our change in interest, if any.
  if (sub->second->sub_state_.subscriptionUpdatePending()) {
    trySendDiscoveryRequests();
  }
}

void NewGrpcMuxImpl::requestOnDemandUpdate(const std::string& type_url,
                                           const absl::flat_hash_set<std::string>& for_update) {
  auto sub = subscriptions_.find(type_url);
  RELEASE_ASSERT(sub != subscriptions_.end(),
                 fmt::format("Watch of {} has no subscription to update.", type_url));
  sub->second->sub_state_.updateSubscriptionInterest(for_update, {});
  // Tell the server about our change in interest, if any.
  if (sub->second->sub_state_.subscriptionUpdatePending()) {
    trySendDiscoveryRequests();
  }
}

void NewGrpcMuxImpl::removeWatch(const std::string& type_url, Watch* watch) {
  updateWatch(type_url, watch, {}, {});
  auto entry = subscriptions_.find(type_url);
  ASSERT(entry != subscriptions_.end(),
         fmt::format("removeWatch() called for non-existent subscription {}.", type_url));
  entry->second->watch_map_.removeWatch(watch);
}

void NewGrpcMuxImpl::addSubscription(const std::string& type_url,
                                     const bool use_namespace_matching) {
  // Resource cache is only used for EDS resources.
  EdsResourcesCacheOptRef resources_cache{absl::nullopt};
  if (eds_resources_cache_ &&
      (type_url == Config::getTypeUrl<envoy::config::endpoint::v3::ClusterLoadAssignment>())) {
    resources_cache = makeOptRefFromPtr(eds_resources_cache_.get());
  }
  subscriptions_.emplace(
      type_url, std::make_unique<SubscriptionStuff>(type_url, local_info_, use_namespace_matching,
                                                    dispatcher_, config_validators_.get(),
                                                    xds_config_tracker_, resources_cache));
  subscription_ordering_.emplace_back(type_url);
}

void NewGrpcMuxImpl::trySendDiscoveryRequests() {
  if (shutdown_) {
    return;
  }

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
    grpc_stream_->sendMessage(request);
  }
  grpc_stream_->maybeUpdateQueueSizeStat(pausable_ack_queue_.size());
}

// Checks whether external conditions allow sending a DeltaDiscoveryRequest. (Does not check
// whether we *want* to send a DeltaDiscoveryRequest).
bool NewGrpcMuxImpl::canSendDiscoveryRequest(const std::string& type_url) {
  RELEASE_ASSERT(
      !pausable_ack_queue_.paused(type_url),
      fmt::format("canSendDiscoveryRequest() called on paused type_url {}. Pausedness is "
                  "supposed to be filtered out by whoWantsToSendDiscoveryRequest(). ",
                  type_url));

  if (!grpc_stream_->grpcStreamAvailable()) {
    ENVOY_LOG(trace, "No stream available to send a discovery request for {}.", type_url);
    return false;
  } else if (!grpc_stream_->checkRateLimitAllowsDrain()) {
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

// A factory class for creating NewGrpcMuxImpl so it does not have to be
// hard-compiled into cluster_manager_impl.cc
class NewGrpcMuxFactory : public MuxFactory {
public:
  std::string name() const override { return "envoy.config_mux.new_grpc_mux_factory"; }
  void shutdownAll() override { return NewGrpcMuxImpl::shutdownAll(); }
  std::shared_ptr<GrpcMux>
  create(Grpc::RawAsyncClientPtr&& async_client, Grpc::RawAsyncClientPtr&& failover_async_client,
         Event::Dispatcher& dispatcher, Random::RandomGenerator&, Stats::Scope& scope,
         const envoy::config::core::v3::ApiConfigSource& ads_config,
         const LocalInfo::LocalInfo& local_info, CustomConfigValidatorsPtr&& config_validators,
         BackOffStrategyPtr&& backoff_strategy, XdsConfigTrackerOptRef xds_config_tracker,
         OptRef<XdsResourcesDelegate>, bool use_eds_resources_cache) override {
    absl::StatusOr<RateLimitSettings> rate_limit_settings_or_error =
        Utility::parseRateLimitSettings(ads_config);
    THROW_IF_NOT_OK_REF(rate_limit_settings_or_error.status());
    GrpcMuxContext grpc_mux_context{
        /*async_client_=*/std::move(async_client),
        /*failover_async_client_=*/std::move(failover_async_client),
        /*dispatcher_=*/dispatcher,
        /*service_method_=*/
        *Protobuf::DescriptorPool::generated_pool()->FindMethodByName(
            "envoy.service.discovery.v3.AggregatedDiscoveryService.DeltaAggregatedResources"),
        /*local_info_=*/local_info,
        /*rate_limit_settings_=*/rate_limit_settings_or_error.value(),
        /*scope_=*/scope,
        /*config_validators_=*/std::move(config_validators),
        /*xds_resources_delegate_=*/absl::nullopt,
        /*xds_config_tracker_=*/xds_config_tracker,
        /*backoff_strategy_=*/std::move(backoff_strategy),
        /*target_xds_authority_=*/"",
        /*eds_resources_cache_=*/
        (use_eds_resources_cache &&
         Runtime::runtimeFeatureEnabled("envoy.restart_features.use_eds_cache_for_ads"))
            ? std::make_unique<EdsResourcesCacheImpl>(dispatcher)
            : nullptr};
    return std::make_shared<Config::NewGrpcMuxImpl>(grpc_mux_context);
  }
};

REGISTER_FACTORY(NewGrpcMuxFactory, MuxFactory);

} // namespace Config
} // namespace Envoy
