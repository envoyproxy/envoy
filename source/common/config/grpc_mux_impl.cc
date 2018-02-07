#include "common/config/grpc_mux_impl.h"

#include <unordered_set>

#include "common/config/utility.h"
#include "common/protobuf/protobuf.h"

namespace Envoy {
namespace Config {

GrpcMuxImpl::GrpcMuxImpl(const envoy::api::v2::core::Node& node, Grpc::AsyncClientPtr async_client,
                         Event::Dispatcher& dispatcher,
                         const Protobuf::MethodDescriptor& service_method)
    : node_(node), async_client_(std::move(async_client)), service_method_(service_method) {
  retry_timer_ = dispatcher.createTimer([this]() -> void { establishNewStream(); });
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
  retry_timer_->enableTimer(std::chrono::milliseconds(RETRY_DELAY_MS));
}

void GrpcMuxImpl::establishNewStream() {
  ENVOY_LOG(debug, "Establishing new gRPC bidi stream for {}", service_method_.DebugString());
  stream_ = async_client_->start(service_method_, *this);
  if (stream_ == nullptr) {
    ENVOY_LOG(warn, "Unable to establish new stream");
    handleFailure();
    return;
  }

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
  if (!api_state_[type_url].subscribed_) {
    api_state_[type_url].request_.set_type_url(type_url);
    api_state_[type_url].request_.mutable_node()->MergeFrom(node_);
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
  const std::string& type_url = message->type_url();
  ENVOY_LOG(debug, "Received gRPC message for {} at version {}", type_url, message->version_info());
  if (api_state_.count(type_url) == 0) {
    ENVOY_LOG(warn, "Ignoring unknown type URL {}", type_url);
    return;
  }
  try {
    // To avoid O(n^2) explosion (e.g. when we have 1000s of EDS watches), we
    // build a map here from resource name to resource and then walk watches_.
    // We have to walk all watches (and need an efficient map as a result) to
    // ensure we deliver empty config updates when a resource is dropped.
    std::unordered_map<std::string, ProtobufWkt::Any> resources;
    for (const auto& resource : message->resources()) {
      if (type_url != resource.type_url()) {
        throw EnvoyException(fmt::format("{} does not match {} type URL is DiscoveryResponse {}",
                                         resource.type_url(), type_url, message->DebugString()));
      }
      const std::string resource_name = Utility::resourceName(resource);
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
    api_state_[type_url].request_.set_version_info(message->version_info());
  } catch (const EnvoyException& e) {
    ENVOY_LOG(warn, "gRPC config for {} update rejected: {}", message->type_url(), e.what());
    for (auto watch : api_state_[type_url].watches_) {
      watch->callbacks_.onConfigUpdateFailed(&e);
    }
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
  handleFailure();
}

} // namespace Config
} // namespace Envoy
