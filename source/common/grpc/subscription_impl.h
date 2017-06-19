#pragma once

#include "envoy/config/subscription.h"
#include "envoy/event/dispatcher.h"

#include "common/grpc/async_client_impl.h"

#include "api/base.pb.h"

namespace Envoy {
namespace Grpc {

template <class ResourceType>
class SubscriptionImpl : public Config::Subscription<ResourceType>,
                         AsyncClientCallbacks<envoy::api::v2::DiscoveryResponse> {
public:
  SubscriptionImpl(const envoy::api::v2::Node& node, Upstream::ClusterManager& cm,
                   const std::string& remote_cluster_name, Event::Dispatcher& dispatcher,
                   const google::protobuf::MethodDescriptor& service_method)
      : SubscriptionImpl(node, std::unique_ptr<AsyncClientImpl<envoy::api::v2::DiscoveryRequest,
                                                               envoy::api::v2::DiscoveryResponse>>(
                                   new AsyncClientImpl<envoy::api::v2::DiscoveryRequest,
                                                       envoy::api::v2::DiscoveryResponse>(
                                       cm, remote_cluster_name)),
                         dispatcher, service_method) {}

  SubscriptionImpl(const envoy::api::v2::Node& node,
                   std::unique_ptr<AsyncClient<envoy::api::v2::DiscoveryRequest,
                                               envoy::api::v2::DiscoveryResponse>> async_client,
                   Event::Dispatcher& dispatcher,
                   const google::protobuf::MethodDescriptor& service_method)
      : async_client_(std::move(async_client)), service_method_(service_method),
        retry_timer_(dispatcher.createTimer([this]() -> void { establishNewStream(); })) {
    request_.mutable_node()->CopyFrom(node);
  }

  void setRetryTimer() { retry_timer_->enableTimer(std::chrono::milliseconds(RETRY_DELAY_MS)); }

  void establishNewStream() {
    stream_ = async_client_->start(service_method_, *this, Optional<std::chrono::milliseconds>());
    if (stream_ == nullptr) {
      // TODO(htuch): Track stats and log failures.
      setRetryTimer();
      return;
    }
    sendDiscoveryRequest();
  }

  void sendDiscoveryRequest() {
    if (stream_ == nullptr) {
      return;
    }
    stream_->sendMessage(request_);
  }

  // Config::Subscription
  void start(const std::vector<std::string>& resources,
             Config::SubscriptionCallbacks<ResourceType>& callbacks) override {
    ASSERT(callbacks_ == nullptr);
    google::protobuf::RepeatedPtrField<std::string> resources_vector(resources.begin(),
                                                                     resources.end());
    request_.mutable_resource_names()->Swap(&resources_vector);
    callbacks_ = &callbacks;
    establishNewStream();
  }

  void updateResources(const std::vector<std::string>& resources) override {
    google::protobuf::RepeatedPtrField<std::string> resources_vector(resources.begin(),
                                                                     resources.end());
    request_.mutable_resource_names()->Swap(&resources_vector);
    sendDiscoveryRequest();
  }

  // Grpc::AsyncClientCallbacks
  void onCreateInitialMetadata(Http::HeaderMap& metadata) override {
    UNREFERENCED_PARAMETER(metadata);
  }

  void onReceiveInitialMetadata(Http::HeaderMapPtr&& metadata) override {
    UNREFERENCED_PARAMETER(metadata);
  }

  void onReceiveMessage(std::unique_ptr<envoy::api::v2::DiscoveryResponse>&& message) override {
    google::protobuf::RepeatedPtrField<ResourceType> typed_resources;
    for (auto& resource : message->resources()) {
      auto* typed_resource = typed_resources.Add();
      resource.UnpackTo(typed_resource);
    }
    if (callbacks_->onConfigUpdate(typed_resources)) {
      request_.set_version_info(message->version_info());
    }
    // This effectively ACK/NACKs the accepted configuration.
    sendDiscoveryRequest();
  }

  void onReceiveTrailingMetadata(Http::HeaderMapPtr&& metadata) override {
    UNREFERENCED_PARAMETER(metadata);
  }

  void onRemoteClose(Status::GrpcStatus status) override {
    // TODO(htuch): Track stats and log failures.
    UNREFERENCED_PARAMETER(status);
    stream_ = nullptr;
    setRetryTimer();
  }

  // TODO(htuch): Make this configurable or some static.
  const uint32_t RETRY_DELAY_MS = 5000;

private:
  std::unique_ptr<AsyncClient<envoy::api::v2::DiscoveryRequest, envoy::api::v2::DiscoveryResponse>>
      async_client_;
  const google::protobuf::MethodDescriptor& service_method_;
  Event::TimerPtr retry_timer_;
  google::protobuf::RepeatedPtrField<std::string> resources_;
  Config::SubscriptionCallbacks<ResourceType>* callbacks_{};
  AsyncClientStream<envoy::api::v2::DiscoveryRequest>* stream_{};
  envoy::api::v2::DiscoveryRequest request_;
};

} // namespace Grpc
} // namespace Envoy
