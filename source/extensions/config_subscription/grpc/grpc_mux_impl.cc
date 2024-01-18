#include "source/extensions/config_subscription/grpc/grpc_mux_impl.h"

#include "envoy/service/discovery/v3/discovery.pb.h"

#include "source/common/config/decoded_resource_impl.h"
#include "source/common/config/utility.h"
#include "source/common/memory/utils.h"
#include "source/common/protobuf/protobuf.h"
#include "source/extensions/config_subscription/grpc/eds_resources_cache_impl.h"
#include "source/extensions/config_subscription/grpc/xds_source_id.h"

#include "absl/container/btree_map.h"
#include "absl/container/node_hash_set.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"

namespace Envoy {
namespace Config {

namespace {
class AllMuxesState {
public:
  void insert(GrpcMuxImpl* mux) { muxes_.insert(mux); }

  void erase(GrpcMuxImpl* mux) { muxes_.erase(mux); }

  void shutdownAll() {
    for (auto& mux : muxes_) {
      mux->shutdown();
    }
  }

private:
  absl::flat_hash_set<GrpcMuxImpl*> muxes_;
};
using AllMuxes = ThreadSafeSingleton<AllMuxesState>;

// Returns true if `resource_name` contains the wildcard XdsTp resource, for example:
// xdstp://test/envoy.config.cluster.v3.Cluster/foo-cluster/*
// xdstp://test/envoy.config.cluster.v3.Cluster/foo-cluster/*?node.name=my_node
bool isXdsTpWildcard(const std::string& resource_name) {
  return XdsResourceIdentifier::hasXdsTpScheme(resource_name) &&
         (absl::EndsWith(resource_name, "/*") || absl::StrContains(resource_name, "/*?"));
}

// Converts the XdsTp resource name to its wildcard equivalent.
// Must only be called on XdsTp resource names.
std::string convertToWildcard(const std::string& resource_name) {
  ASSERT(XdsResourceIdentifier::hasXdsTpScheme(resource_name));
  auto resource_or_error = XdsResourceIdentifier::decodeUrn(resource_name);
  THROW_IF_STATUS_NOT_OK(resource_or_error, throw);
  xds::core::v3::ResourceName xdstp_resource = resource_or_error.value();
  const auto pos = xdstp_resource.id().find_last_of('/');
  xdstp_resource.set_id(
      pos == std::string::npos ? "*" : absl::StrCat(xdstp_resource.id().substr(0, pos), "/*"));
  XdsResourceIdentifier::EncodeOptions options;
  options.sort_context_params_ = true;
  return XdsResourceIdentifier::encodeUrn(xdstp_resource, options);
}
} // namespace

GrpcMuxImpl::GrpcMuxImpl(GrpcMuxContext& grpc_mux_context, bool skip_subsequent_node)
    : grpc_stream_(this, std::move(grpc_mux_context.async_client_),
                   grpc_mux_context.service_method_, grpc_mux_context.dispatcher_,
                   grpc_mux_context.scope_, std::move(grpc_mux_context.backoff_strategy_),
                   grpc_mux_context.rate_limit_settings_),
      local_info_(grpc_mux_context.local_info_), skip_subsequent_node_(skip_subsequent_node),
      config_validators_(std::move(grpc_mux_context.config_validators_)),
      xds_config_tracker_(grpc_mux_context.xds_config_tracker_),
      xds_resources_delegate_(grpc_mux_context.xds_resources_delegate_),
      eds_resources_cache_(std::move(grpc_mux_context.eds_resources_cache_)),
      target_xds_authority_(grpc_mux_context.target_xds_authority_),
      dispatcher_(grpc_mux_context.dispatcher_),
      dynamic_update_callback_handle_(
          grpc_mux_context.local_info_.contextProvider().addDynamicContextUpdateCallback(
              [this](absl::string_view resource_type_url) {
                onDynamicContextUpdate(resource_type_url);
              })) {
  Config::Utility::checkLocalInfo("ads", local_info_);
  AllMuxes::get().insert(this);
}

GrpcMuxImpl::~GrpcMuxImpl() { AllMuxes::get().erase(this); }

void GrpcMuxImpl::shutdownAll() { AllMuxes::get().shutdownAll(); }

void GrpcMuxImpl::onDynamicContextUpdate(absl::string_view resource_type_url) {
  auto api_state = api_state_.find(resource_type_url);
  if (api_state == api_state_.end()) {
    return;
  }
  api_state->second->must_send_node_ = true;
  queueDiscoveryRequest(resource_type_url);
}

void GrpcMuxImpl::start() {
  ASSERT(!started_);
  if (started_) {
    return;
  }
  started_ = true;
  grpc_stream_.establishNewStream();
}

void GrpcMuxImpl::sendDiscoveryRequest(absl::string_view type_url) {
  if (shutdown_) {
    return;
  }

  ApiState& api_state = apiStateFor(type_url);
  auto& request = api_state.request_;
  request.mutable_resource_names()->Clear();

  // Maintain a set to avoid dupes.
  absl::node_hash_set<std::string> resources;
  for (const auto* watch : api_state.watches_) {
    for (const std::string& resource : watch->resources_) {
      if (!resources.contains(resource)) {
        resources.emplace(resource);
        request.add_resource_names(resource);
      }
    }
  }

  if (api_state.must_send_node_ || !skip_subsequent_node_ || first_stream_request_) {
    // Node may have been cleared during a previous request.
    request.mutable_node()->CopyFrom(local_info_.node());
    api_state.must_send_node_ = false;
  } else {
    request.clear_node();
  }
  ENVOY_LOG(trace, "Sending DiscoveryRequest for {}: {}", type_url, request.ShortDebugString());
  grpc_stream_.sendMessage(request);
  first_stream_request_ = false;

  // clear error_detail after the request is sent if it exists.
  if (apiStateFor(type_url).request_.has_error_detail()) {
    apiStateFor(type_url).request_.clear_error_detail();
  }
}

void GrpcMuxImpl::clearNonce() {
  // Iterate over all api_states (for each type_url), and clear its nonce.
  for (auto& [type_url, api_state] : api_state_) {
    if (api_state) {
      api_state->request_.clear_response_nonce();
    }
  }
}

void GrpcMuxImpl::loadConfigFromDelegate(const std::string& type_url,
                                         const absl::flat_hash_set<std::string>& resource_names) {
  if (!xds_resources_delegate_.has_value()) {
    return;
  }
  ApiState& api_state = apiStateFor(type_url);
  if (api_state.watches_.empty()) {
    // No watches, so exit without loading config from storage.
    return;
  }

  const XdsConfigSourceId source_id{target_xds_authority_, type_url};
  TRY_ASSERT_MAIN_THREAD {
    std::vector<envoy::service::discovery::v3::Resource> resources =
        xds_resources_delegate_->getResources(source_id, resource_names);
    if (resources.empty()) {
      // There are no persisted resources, so nothing to process.
      return;
    }

    std::vector<DecodedResourcePtr> decoded_resources;
    OpaqueResourceDecoder& resource_decoder = *api_state.watches_.front()->resource_decoder_;
    std::string version_info;
    for (const auto& resource : resources) {
      if (version_info.empty()) {
        version_info = resource.version();
      } else {
        ASSERT(resource.version() == version_info);
      }

      TRY_ASSERT_MAIN_THREAD {
        decoded_resources.emplace_back(
            std::make_unique<DecodedResourceImpl>(resource_decoder, resource));
      }
      END_TRY
      catch (const EnvoyException& e) {
        xds_resources_delegate_->onResourceLoadFailed(source_id, resource.name(), e);
      }
    }

    processDiscoveryResources(decoded_resources, api_state, type_url, version_info,
                              /*call_delegate=*/false);
  }
  END_TRY
  catch (const EnvoyException& e) {
    // TODO(abeyad): do something else here?
    ENVOY_LOG_MISC(warn, "Failed to load config from delegate for {}: {}", source_id.toKey(),
                   e.what());
  }
}

GrpcMuxWatchPtr GrpcMuxImpl::addWatch(const std::string& type_url,
                                      const absl::flat_hash_set<std::string>& resources,
                                      SubscriptionCallbacks& callbacks,
                                      OpaqueResourceDecoderSharedPtr resource_decoder,
                                      const SubscriptionOptions& options) {
  // Resource cache is only used for EDS resources.
  EdsResourcesCacheOptRef resources_cache{absl::nullopt};
  if (eds_resources_cache_ &&
      (type_url == Config::getTypeUrl<envoy::config::endpoint::v3::ClusterLoadAssignment>())) {
    resources_cache = makeOptRefFromPtr(eds_resources_cache_.get());
  }
  auto watch = std::make_unique<GrpcMuxWatchImpl>(resources, callbacks, resource_decoder, type_url,
                                                  *this, options, local_info_, resources_cache);
  ENVOY_LOG(debug, "gRPC mux addWatch for " + type_url);

  // Lazily kick off the requests based on first subscription. This has the
  // convenient side-effect that we order messages on the channel based on
  // Envoy's internal dependency ordering.
  // TODO(gsagula): move TokenBucketImpl params to a config.
  if (!apiStateFor(type_url).subscribed_) {
    apiStateFor(type_url).request_.set_type_url(type_url);
    apiStateFor(type_url).request_.mutable_node()->MergeFrom(local_info_.node());
    apiStateFor(type_url).subscribed_ = true;
    subscriptions_.emplace_back(type_url);
  }

  // This will send an updated request on each subscription.
  // TODO(htuch): For RDS/EDS, this will generate a new DiscoveryRequest on each resource we added.
  // Consider in the future adding some kind of collation/batching during CDS/LDS updates so that we
  // only send a single RDS/EDS update after the CDS/LDS update.
  queueDiscoveryRequest(type_url);

  return watch;
}

ScopedResume GrpcMuxImpl::pause(const std::string& type_url) {
  return pause(std::vector<std::string>{type_url});
}

ScopedResume GrpcMuxImpl::pause(const std::vector<std::string> type_urls) {
  for (const auto& type_url : type_urls) {
    ApiState& api_state = apiStateFor(type_url);
    ENVOY_LOG(debug, "Pausing discovery requests for {} (previous count {})", type_url,
              api_state.pauses_);
    ++api_state.pauses_;
  }
  return std::make_unique<Cleanup>([this, type_urls]() {
    for (const auto& type_url : type_urls) {
      ApiState& api_state = apiStateFor(type_url);
      ENVOY_LOG(debug, "Decreasing pause count on discovery requests for {} (previous count {})",
                type_url, api_state.pauses_);
      ASSERT(api_state.paused());

      if (--api_state.pauses_ == 0 && api_state.pending_ && api_state.subscribed_) {
        ENVOY_LOG(debug, "Resuming discovery requests for {}", type_url);
        queueDiscoveryRequest(type_url);
        api_state.pending_ = false;
      }
    }
  });
}

void GrpcMuxImpl::onDiscoveryResponse(
    std::unique_ptr<envoy::service::discovery::v3::DiscoveryResponse>&& message,
    ControlPlaneStats& control_plane_stats) {
  const std::string type_url = message->type_url();
  ENVOY_LOG(debug, "Received gRPC message for {} at version {}", type_url, message->version_info());

  if (api_state_.count(type_url) == 0) {
    // TODO(yuval-k): This should never happen. consider dropping the stream as this is a
    // protocol violation
    ENVOY_LOG(warn, "Ignoring the message for type URL {} as it has no current subscribers.",
              type_url);
    return;
  }

  ApiState& api_state = apiStateFor(type_url);

  if (message->has_control_plane()) {
    control_plane_stats.identifier_.set(message->control_plane().identifier());

    if (message->control_plane().identifier() != api_state.control_plane_identifier_) {
      api_state.control_plane_identifier_ = message->control_plane().identifier();
      ENVOY_LOG(debug, "Receiving gRPC updates for {} from {}", type_url,
                api_state.control_plane_identifier_);
    }
  }

  if (api_state.watches_.empty()) {
    // update the nonce as we are processing this response.
    api_state.request_.set_response_nonce(message->nonce());
    if (message->resources().empty()) {
      // No watches and no resources. This can happen when envoy unregisters from a
      // resource that's removed from the server as well. For example, a deleted cluster
      // triggers un-watching the ClusterLoadAssignment watch, and at the same time the
      // xDS server sends an empty list of ClusterLoadAssignment resources. we'll accept
      // this update. no need to send a discovery request, as we don't watch for anything.
      api_state.request_.set_version_info(message->version_info());
    } else {
      // No watches and we have resources - this should not happen. send a NACK (by not
      // updating the version).
      ENVOY_LOG(warn, "Ignoring unwatched type URL {}", type_url);
      queueDiscoveryRequest(type_url);
    }
    return;
  }
  ScopedResume same_type_resume;
  // We pause updates of the same type. This is necessary for SotW and GrpcMuxImpl, since unlike
  // delta and NewGRpcMuxImpl, independent watch additions/removals trigger updates regardless of
  // the delta state. The proper fix for this is to converge these implementations,
  // see https://github.com/envoyproxy/envoy/issues/11477.
  same_type_resume = pause(type_url);
  TRY_ASSERT_MAIN_THREAD {
    std::vector<DecodedResourcePtr> resources;
    OpaqueResourceDecoder& resource_decoder = *api_state.watches_.front()->resource_decoder_;

    for (const auto& resource : message->resources()) {
      // TODO(snowp): Check the underlying type when the resource is a Resource.
      if (!resource.Is<envoy::service::discovery::v3::Resource>() &&
          type_url != resource.type_url()) {
        throw EnvoyException(
            fmt::format("{} does not match the message-wide type URL {} in DiscoveryResponse {}",
                        resource.type_url(), type_url, message->DebugString()));
      }

      auto decoded_resource =
          DecodedResourceImpl::fromResource(resource_decoder, resource, message->version_info());

      if (!isHeartbeatResource(type_url, *decoded_resource)) {
        resources.emplace_back(std::move(decoded_resource));
      }
    }

    processDiscoveryResources(resources, api_state, type_url, message->version_info(),
                              /*call_delegate=*/true);

    // Processing point when resources are successfully ingested.
    if (xds_config_tracker_.has_value()) {
      xds_config_tracker_->onConfigAccepted(type_url, resources);
    }
  }
  END_TRY
  catch (const EnvoyException& e) {
    for (auto watch : api_state.watches_) {
      watch->callbacks_.onConfigUpdateFailed(
          Envoy::Config::ConfigUpdateFailureReason::UpdateRejected, &e);
    }
    ::google::rpc::Status* error_detail = api_state.request_.mutable_error_detail();
    error_detail->set_code(Grpc::Status::WellKnownGrpcStatus::Internal);
    error_detail->set_message(Config::Utility::truncateGrpcStatusMessage(e.what()));

    // Processing point when there is any exception during the parse and ingestion process.
    if (xds_config_tracker_.has_value()) {
      xds_config_tracker_->onConfigRejected(*message, error_detail->message());
    }
  }
  api_state.previously_fetched_data_ = true;
  api_state.request_.set_response_nonce(message->nonce());
  ASSERT(api_state.paused());
  queueDiscoveryRequest(type_url);
}

void GrpcMuxImpl::processDiscoveryResources(const std::vector<DecodedResourcePtr>& resources,
                                            ApiState& api_state, const std::string& type_url,
                                            const std::string& version_info,
                                            const bool call_delegate) {
  ASSERT_IS_MAIN_OR_TEST_THREAD();
  // To avoid O(n^2) explosion (e.g. when we have 1000s of EDS watches), we
  // build a map here from resource name to resource and then walk watches_.
  // We have to walk all watches (and need an efficient map as a result) to
  // ensure we deliver empty config updates when a resource is dropped. We make the map ordered
  // for test determinism.
  absl::btree_map<std::string, DecodedResourceRef> resource_ref_map;
  std::vector<DecodedResourceRef> all_resource_refs;

  const auto scoped_ttl_update = api_state.ttl_.scopedTtlUpdate();

  for (const auto& resource : resources) {
    if (resource->ttl()) {
      api_state.ttl_.add(*resource->ttl(), resource->name());
    } else {
      api_state.ttl_.clear(resource->name());
    }

    all_resource_refs.emplace_back(*resource);
    if (XdsResourceIdentifier::hasXdsTpScheme(resource->name())) {
      // Sort the context params of an xdstp resource, so we can compare them easily.
      auto resource_or_error = XdsResourceIdentifier::decodeUrn(resource->name());
      THROW_IF_STATUS_NOT_OK(resource_or_error, throw);
      xds::core::v3::ResourceName xdstp_resource = resource_or_error.value();
      XdsResourceIdentifier::EncodeOptions options;
      options.sort_context_params_ = true;
      resource_ref_map.emplace(XdsResourceIdentifier::encodeUrn(xdstp_resource, options),
                               *resource);
    } else {
      resource_ref_map.emplace(resource->name(), *resource);
    }
  }

  // Execute external config validators if there are any watches.
  if (!api_state.watches_.empty()) {
    config_validators_->executeValidators(type_url, resources);
  }

  for (auto watch : api_state.watches_) {
    // onConfigUpdate should be called in all cases for single watch xDS (Cluster and
    // Listener) even if the message does not have resources so that update_empty stat
    // is properly incremented and state-of-the-world semantics are maintained.
    if (watch->resources_.empty()) {
      THROW_IF_NOT_OK(watch->callbacks_.onConfigUpdate(all_resource_refs, version_info));
      continue;
    }
    std::vector<DecodedResourceRef> found_resources;
    for (const auto& watched_resource_name : watch->resources_) {
      // Look for a singleton subscription.
      auto it = resource_ref_map.find(watched_resource_name);
      if (it != resource_ref_map.end()) {
        found_resources.emplace_back(it->second);
      } else if (isXdsTpWildcard(watched_resource_name)) {
        // See if the resources match the xdstp wildcard subscription.
        // Note: although it is unlikely that Envoy will need to support a resource that is mapped
        // to both a singleton and collection watch, this code still supports this use case.
        // TODO(abeyad): This could be made more efficient, e.g. by pre-computing and having a map
        // entry for each wildcard watch.
        for (const auto& resource_ref_it : resource_ref_map) {
          if (XdsResourceIdentifier::hasXdsTpScheme(resource_ref_it.first) &&
              convertToWildcard(resource_ref_it.first) == watched_resource_name) {
            found_resources.emplace_back(resource_ref_it.second);
          }
        }
      }
    }

    // onConfigUpdate should be called only on watches(clusters/listeners) that have
    // updates in the message for EDS/RDS.
    if (!found_resources.empty()) {
      THROW_IF_NOT_OK(watch->callbacks_.onConfigUpdate(found_resources, version_info));
      // Resource cache is only used for EDS resources.
      if (eds_resources_cache_ &&
          (type_url == Config::getTypeUrl<envoy::config::endpoint::v3::ClusterLoadAssignment>())) {
        for (const auto& resource : found_resources) {
          const envoy::config::endpoint::v3::ClusterLoadAssignment& cluster_load_assignment =
              dynamic_cast<const envoy::config::endpoint::v3::ClusterLoadAssignment&>(
                  resource.get().resource());
          eds_resources_cache_->setResource(resource.get().name(), cluster_load_assignment);
        }
        // No need to remove resources from the cache, as currently only non-collection
        // subscriptions are supported, and these resources are removed in the call
        // to updateWatchInterest().
      }
    }
  }

  // All config updates have been applied without throwing an exception, so we'll call the xDS
  // resources delegate, if any.
  if (call_delegate && xds_resources_delegate_.has_value()) {
    xds_resources_delegate_->onConfigUpdated(XdsConfigSourceId{target_xds_authority_, type_url},
                                             all_resource_refs);
  }

  // TODO(mattklein123): In the future if we start tracking per-resource versions, we
  // would do that tracking here.
  api_state.request_.set_version_info(version_info);
  Memory::Utils::tryShrinkHeap();
}

void GrpcMuxImpl::onWriteable() { drainRequests(); }

void GrpcMuxImpl::onStreamEstablished() {
  first_stream_request_ = true;
  grpc_stream_.maybeUpdateQueueSizeStat(0);
  clearNonce();
  request_queue_ = std::make_unique<std::queue<std::string>>();
  for (const auto& type_url : subscriptions_) {
    queueDiscoveryRequest(type_url);
  }
}

void GrpcMuxImpl::onEstablishmentFailure() {
  for (const auto& api_state : api_state_) {
    for (auto watch : api_state.second->watches_) {
      watch->callbacks_.onConfigUpdateFailed(
          Envoy::Config::ConfigUpdateFailureReason::ConnectionFailure, nullptr);
    }
    if (!api_state.second->previously_fetched_data_) {
      // On the initialization of the gRPC mux, if connection to the xDS server fails, load the
      // persisted config, if available. The locally persisted config will be used until
      // connectivity is established with the xDS server.
      loadConfigFromDelegate(
          /*type_url=*/api_state.first,
          absl::flat_hash_set<std::string>{api_state.second->request_.resource_names().begin(),
                                           api_state.second->request_.resource_names().end()});
      api_state.second->previously_fetched_data_ = true;
    }
  }
}

void GrpcMuxImpl::queueDiscoveryRequest(absl::string_view queue_item) {
  if (!grpc_stream_.grpcStreamAvailable()) {
    ENVOY_LOG(debug, "No stream available to queueDiscoveryRequest for {}", queue_item);
    return; // Drop this request; the reconnect will enqueue a new one.
  }
  ApiState& api_state = apiStateFor(queue_item);
  if (api_state.paused()) {
    ENVOY_LOG(trace, "API {} paused during queueDiscoveryRequest(), setting pending.", queue_item);
    api_state.pending_ = true;
    return; // Drop this request; the unpause will enqueue a new one.
  }
  request_queue_->emplace(std::string(queue_item));
  drainRequests();
}

void GrpcMuxImpl::expiryCallback(absl::string_view type_url,
                                 const std::vector<std::string>& expired) {
  // The TtlManager triggers a callback with a list of all the expired elements, which we need
  // to compare against the various watched resources to return the subset that each watch is
  // subscribed to.

  // We convert the incoming list into a set in order to more efficiently perform this
  // comparison when there are a lot of watches.
  absl::flat_hash_set<std::string> all_expired;
  all_expired.insert(expired.begin(), expired.end());

  // Note: We can blindly dereference the lookup here since the only time we call this is in a
  // callback that is created at the same time as we insert the ApiState for this type.
  for (auto watch : api_state_.find(type_url)->second->watches_) {
    Protobuf::RepeatedPtrField<std::string> found_resources_for_watch;

    for (const auto& resource : expired) {
      if (all_expired.find(resource) != all_expired.end()) {
        found_resources_for_watch.Add(std::string(resource));
      }
    }

    THROW_IF_NOT_OK(watch->callbacks_.onConfigUpdate({}, found_resources_for_watch, ""));
  }
}

GrpcMuxImpl::ApiState& GrpcMuxImpl::apiStateFor(absl::string_view type_url) {
  auto itr = api_state_.find(type_url);
  if (itr == api_state_.end()) {
    api_state_.emplace(
        type_url, std::make_unique<ApiState>(dispatcher_, [this, type_url](const auto& expired) {
          expiryCallback(type_url, expired);
        }));
  }

  return *api_state_.find(type_url)->second;
}

void GrpcMuxImpl::drainRequests() {
  while (!request_queue_->empty() && grpc_stream_.checkRateLimitAllowsDrain()) {
    // Process the request, if rate limiting is not enabled at all or if it is under rate limit.
    sendDiscoveryRequest(request_queue_->front());
    request_queue_->pop();
  }
  grpc_stream_.maybeUpdateQueueSizeStat(request_queue_->size());
}

// A factory class for creating GrpcMuxImpl so it does not have to be
// hard-compiled into cluster_manager_impl.cc
class GrpcMuxFactory : public MuxFactory {
public:
  std::string name() const override { return "envoy.config_mux.grpc_mux_factory"; }
  void shutdownAll() override { return GrpcMuxImpl::shutdownAll(); }
  std::shared_ptr<GrpcMux>
  create(Grpc::RawAsyncClientPtr&& async_client, Event::Dispatcher& dispatcher,
         Random::RandomGenerator&, Stats::Scope& scope,
         const envoy::config::core::v3::ApiConfigSource& ads_config,
         const LocalInfo::LocalInfo& local_info, CustomConfigValidatorsPtr&& config_validators,
         BackOffStrategyPtr&& backoff_strategy, XdsConfigTrackerOptRef xds_config_tracker,
         XdsResourcesDelegateOptRef xds_resources_delegate, bool use_eds_resources_cache) override {
    GrpcMuxContext grpc_mux_context{
        /*async_client_=*/std::move(async_client),
        /*dispatcher_=*/dispatcher,
        /*service_method_=*/
        *Protobuf::DescriptorPool::generated_pool()->FindMethodByName(
            "envoy.service.discovery.v3.AggregatedDiscoveryService.StreamAggregatedResources"),
        /*local_info_=*/local_info,
        /*rate_limit_settings_=*/Utility::parseRateLimitSettings(ads_config),
        /*scope_=*/scope,
        /*config_validators_=*/std::move(config_validators),
        /*xds_resources_delegate_=*/xds_resources_delegate,
        /*xds_config_tracker_=*/xds_config_tracker,
        /*backoff_strategy_=*/std::move(backoff_strategy),
        /*target_xds_authority_=*/Config::Utility::getGrpcControlPlane(ads_config).value_or(""),
        /*eds_resources_cache_=*/
        (use_eds_resources_cache &&
         Runtime::runtimeFeatureEnabled("envoy.restart_features.use_eds_cache_for_ads"))
            ? std::make_unique<EdsResourcesCacheImpl>(dispatcher)
            : nullptr};
    return std::make_shared<Config::GrpcMuxImpl>(grpc_mux_context,
                                                 ads_config.set_node_on_first_message_only());
  }
};

REGISTER_FACTORY(GrpcMuxFactory, MuxFactory);

} // namespace Config
} // namespace Envoy
