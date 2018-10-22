#include "common/config/grpc_mux_impl.h"

#include <unordered_set>

#include "common/common/token_bucket_impl.h"
#include "common/config/utility.h"
#include "common/protobuf/protobuf.h"

namespace Envoy {
namespace Config {

GrpcMuxImpl::GrpcMuxImpl(const LocalInfo::LocalInfo& local_info, Grpc::AsyncClientPtr async_client,
                         Event::Dispatcher& dispatcher,
                         const Protobuf::MethodDescriptor& service_method,
                         Runtime::RandomGenerator& random, Stats::Scope& scope)
    : local_info_(local_info), async_client_(std::move(async_client)),
      service_method_(service_method), random_(random), time_source_(dispatcher.timeSystem()),
      control_plane_stats_(generateControlPlaneStats(scope)) {
  Config::Utility::checkLocalInfo("ads", local_info);
  retry_timer_ = dispatcher.createTimer([this]() -> void { establishNewStream(); });
  backoff_strategy_ = std::make_unique<JitteredBackOffStrategy>(RETRY_INITIAL_DELAY_MS,
                                                                RETRY_MAX_DELAY_MS, random_);
}

GrpcMuxImpl::~GrpcMuxImpl() {
  for (const auto& api_state : api_state_) {
    for (auto watch : api_state.second.watches_) {
      watch->inserted_ = false;
    }
  }
}

void GrpcMuxImpl::start() { establishNewStream(); }

void GrpcMuxImpl::setRetryTimer() {
  retry_timer_->enableTimer(std::chrono::milliseconds(backoff_strategy_->nextBackOffMs()));
}

void GrpcMuxImpl::establishNewStream() {
  ENVOY_LOG(debug, "Establishing new gRPC bidi stream for {}", service_method_.DebugString());
  stream_ = async_client_->start(service_method_, *this);
  if (stream_ == nullptr) {
    ENVOY_LOG(warn, "Unable to establish new stream");
    handleFailure();
    return;
  }

  control_plane_stats_.connected_state_.set(1);
  for (const auto type_url : subscriptions_) {
    sendDiscoveryRequest(type_url);
  }
}

void GrpcMuxImpl::sendDiscoveryRequest(const std::string& type_url) {
  if (stream_ == nullptr) {
    ENVOY_LOG(debug, "No stream available to sendDiscoveryRequest for {}", type_url);
    return;
  }

  ApiState& api_state = api_state_[type_url];
  if (api_state.paused_) {
    ENVOY_LOG(trace, "API {} paused during sendDiscoveryRequest(), setting pending.", type_url);
    api_state.pending_ = true;
    return;
  }

  if (!api_state.limit_request_->consume() && api_state.limit_log_->consume()) {
    ENVOY_LOG(warn, "{}", fmt::format("Too many sendDiscoveryRequest calls for {}", type_url));
  }

  auto& request = api_state.request_;
  request.mutable_resource_names()->Clear();

  // Maintain a set to avoid dupes.
  std::unordered_set<std::string> resources;
  for (const auto* watch : api_state.watches_) {
    for (const std::string& resource : watch->resources_) {
      if (resources.count(resource) == 0) {
        resources.emplace(resource);
        request.add_resource_names(resource);
      }
    }
  }

  ENVOY_LOG(trace, "Sending DiscoveryRequest for {}: {}", type_url, request.DebugString());
  stream_->sendMessage(request, false);

  // clear error_detail after the request is sent if it exists.
  if (api_state_[type_url].request_.has_error_detail()) {
    api_state_[type_url].request_.clear_error_detail();
  }
}

void GrpcMuxImpl::handleFailure() {
  for (const auto& api_state : api_state_) {
    for (auto watch : api_state.second.watches_) {
      watch->callbacks_.onConfigUpdateFailed(nullptr);
    }
  }
  setRetryTimer();
}

GrpcMuxWatchPtr GrpcMuxImpl::subscribe(const std::string& type_url,
                                       const std::vector<std::string>& resources,
                                       GrpcMuxCallbacks& callbacks) {
  auto watch =
      std::unique_ptr<GrpcMuxWatch>(new GrpcMuxWatchImpl(resources, callbacks, type_url, *this));
  ENVOY_LOG(debug, "gRPC mux subscribe for " + type_url);

  // Lazily kick off the requests based on first subscription. This has the
  // convenient side-effect that we order messages on the channel based on
  // Envoy's internal dependency ordering.
  // TODO(gsagula): move TokenBucketImpl params to a config.
  if (!api_state_[type_url].subscribed_) {
    // Bucket contains 100 tokens maximum and refills at 5 tokens/sec.
    api_state_[type_url].limit_request_ = std::make_unique<TokenBucketImpl>(100, time_source_, 5);
    // Bucket contains 1 token maximum and refills 1 token on every ~5 seconds.
    api_state_[type_url].limit_log_ = std::make_unique<TokenBucketImpl>(1, time_source_, 0.2);
    api_state_[type_url].request_.set_type_url(type_url);
    api_state_[type_url].request_.mutable_node()->MergeFrom(local_info_.node());
    api_state_[type_url].subscribed_ = true;
    subscriptions_.emplace_back(type_url);
  }

  // This will send an updated request on each subscription.
  // TODO(htuch): For RDS/EDS, this will generate a new DiscoveryRequest on each resource we added.
  // Consider in the future adding some kind of collation/batching during CDS/LDS updates so that we
  // only send a single RDS/EDS update after the CDS/LDS update.
  sendDiscoveryRequest(type_url);

  return watch;
}

void GrpcMuxImpl::pause(const std::string& type_url) {
  ENVOY_LOG(debug, "Pausing discovery requests for {}", type_url);
  ApiState& api_state = api_state_[type_url];
  ASSERT(!api_state.paused_);
  ASSERT(!api_state.pending_);
  api_state.paused_ = true;
}

void GrpcMuxImpl::resume(const std::string& type_url) {
  ENVOY_LOG(debug, "Resuming discovery requests for {}", type_url);
  ApiState& api_state = api_state_[type_url];
  ASSERT(api_state.paused_);
  api_state.paused_ = false;

  if (api_state.pending_) {
    ASSERT(api_state.subscribed_);
    sendDiscoveryRequest(type_url);
    api_state.pending_ = false;
  }
}

void GrpcMuxImpl::onCreateInitialMetadata(Http::HeaderMap& metadata) {
  UNREFERENCED_PARAMETER(metadata);
}

void GrpcMuxImpl::onReceiveInitialMetadata(Http::HeaderMapPtr&& metadata) {
  UNREFERENCED_PARAMETER(metadata);
}

void GrpcMuxImpl::onReceiveMessage(std::unique_ptr<envoy::api::v2::DiscoveryResponse>&& message) {
  // Reset here so that it starts with fresh backoff interval on next disconnect.
  backoff_strategy_->reset();

  const std::string& type_url = message->type_url();
  ENVOY_LOG(debug, "Received gRPC message for {} at version {}", type_url, message->version_info());
  if (api_state_.count(type_url) == 0) {
    ENVOY_LOG(warn, "Ignoring unknown type URL {}", type_url);
    // TODO(yuval-k): This should never happen. consider dropping the stream as this is a protocol
    // violation
    return;
  }
  if (api_state_[type_url].watches_.empty()) {
    // update the nonce as we are processing this response.
    api_state_[type_url].request_.set_response_nonce(message->nonce());
    if (message->resources().empty()) {
      // No watches and no resources. This can happen when envoy unregisters from a resource
      // that's removed from the server as well. For example, a deleted cluster triggers un-watching
      // the ClusterLoadAssignment watch, and at the same time the xDS server sends an empty list of
      // ClusterLoadAssignment resources. we'll accept this update. no need to send a discovery
      // request, as we don't watch for anything.
      api_state_[type_url].request_.set_version_info(message->version_info());
    } else {
      // No watches and we have resources - this should not happen. send a NACK (by not updating
      // the version).
      ENVOY_LOG(warn, "Ignoring unwatched type URL {}", type_url);
      sendDiscoveryRequest(type_url);
    }
    return;
  }
  try {
    // To avoid O(n^2) explosion (e.g. when we have 1000s of EDS watches), we
    // build a map here from resource name to resource and then walk watches_.
    // We have to walk all watches (and need an efficient map as a result) to
    // ensure we deliver empty config updates when a resource is dropped.
    std::unordered_map<std::string, ProtobufWkt::Any> resources;
    GrpcMuxCallbacks& callbacks = api_state_[type_url].watches_.front()->callbacks_;
    for (const auto& resource : message->resources()) {
      if (type_url != resource.type_url()) {
        throw EnvoyException(fmt::format("{} does not match {} type URL is DiscoveryResponse {}",
                                         resource.type_url(), type_url, message->DebugString()));
      }
      const std::string resource_name = callbacks.resourceName(resource);
      resources.emplace(resource_name, resource);
    }
    for (auto watch : api_state_[type_url].watches_) {
      if (watch->resources_.empty()) {
        watch->callbacks_.onConfigUpdate(message->resources(), message->version_info());
        continue;
      }
      Protobuf::RepeatedPtrField<ProtobufWkt::Any> found_resources;
      for (auto watched_resource_name : watch->resources_) {
        auto it = resources.find(watched_resource_name);
        if (it != resources.end()) {
          found_resources.Add()->MergeFrom(it->second);
        }
      }
      watch->callbacks_.onConfigUpdate(found_resources, message->version_info());
    }
    // TODO(mattklein123): In the future if we start tracking per-resource versions, we would do
    // that tracking here.
    api_state_[type_url].request_.set_version_info(message->version_info());
  } catch (const EnvoyException& e) {
    for (auto watch : api_state_[type_url].watches_) {
      watch->callbacks_.onConfigUpdateFailed(&e);
    }
    ::google::rpc::Status* error_detail = api_state_[type_url].request_.mutable_error_detail();
    error_detail->set_code(Grpc::Status::GrpcStatus::Internal);
    error_detail->set_message(e.what());
  }
  api_state_[type_url].request_.set_response_nonce(message->nonce());
  sendDiscoveryRequest(type_url);
}

void GrpcMuxImpl::onReceiveTrailingMetadata(Http::HeaderMapPtr&& metadata) {
  UNREFERENCED_PARAMETER(metadata);
}

void GrpcMuxImpl::onRemoteClose(Grpc::Status::GrpcStatus status, const std::string& message) {
  ENVOY_LOG(warn, "gRPC config stream closed: {}, {}", status, message);
  stream_ = nullptr;
  control_plane_stats_.connected_state_.set(0);
  setRetryTimer();
}

} // namespace Config
} // namespace Envoy
