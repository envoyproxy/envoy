#pragma once

#include "envoy/config/subscription.h"
#include "envoy/event/dispatcher.h"

#include "common/common/logger.h"
#include "common/config/utility.h"
#include "common/grpc/async_client_impl.h"
#include "common/protobuf/protobuf.h"

#include "api/base.pb.h"

namespace Envoy {
namespace Config {

template <class ResourceType>
class GrpcSubscriptionImpl : public Config::Subscription<ResourceType>,
                             Grpc::AsyncStreamCallbacks<envoy::api::v2::DiscoveryResponse>,
                             Logger::Loggable<Logger::Id::config> {
public:
  GrpcSubscriptionImpl(const envoy::api::v2::Node& node, Upstream::ClusterManager& cm,
                       const std::string& remote_cluster_name, Event::Dispatcher& dispatcher,
                       const Protobuf::MethodDescriptor& service_method, SubscriptionStats stats)
      : GrpcSubscriptionImpl(
            node,
            std::unique_ptr<Grpc::AsyncClientImpl<envoy::api::v2::DiscoveryRequest,
                                                  envoy::api::v2::DiscoveryResponse>>(
                new Grpc::AsyncClientImpl<envoy::api::v2::DiscoveryRequest,
                                          envoy::api::v2::DiscoveryResponse>(cm,
                                                                             remote_cluster_name)),
            dispatcher, service_method, stats) {}

  GrpcSubscriptionImpl(const envoy::api::v2::Node& node,
                       std::unique_ptr<Grpc::AsyncClient<envoy::api::v2::DiscoveryRequest,
                                                         envoy::api::v2::DiscoveryResponse>>
                           async_client,
                       Event::Dispatcher& dispatcher,
                       const Protobuf::MethodDescriptor& service_method, SubscriptionStats stats)
      : async_client_(std::move(async_client)), service_method_(service_method),
        retry_timer_(dispatcher.createTimer([this]() -> void { establishNewStream(); })),
        stats_(stats) {
    request_.mutable_node()->CopyFrom(node);
  }

  void setRetryTimer() { retry_timer_->enableTimer(std::chrono::milliseconds(RETRY_DELAY_MS)); }

  void establishNewStream() {
    ENVOY_LOG(debug, "Establishing new gRPC bidi stream for {}",
              ProtobufTypes::FromString(service_method_.DebugString()));
    stats_.update_attempt_.inc();
    stream_ = async_client_->start(service_method_, *this, Optional<std::chrono::milliseconds>());
    if (stream_ == nullptr) {
      ENVOY_LOG(warn, "Unable to establish new stream");
      handleFailure();
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
    Protobuf::RepeatedPtrField<ProtobufTypes::String> resources_vector(resources.begin(),
                                                                       resources.end());
    request_.mutable_resource_names()->Swap(&resources_vector);
    callbacks_ = &callbacks;
    establishNewStream();
  }

  void updateResources(const std::vector<std::string>& resources) override {
    Protobuf::RepeatedPtrField<ProtobufTypes::String> resources_vector(resources.begin(),
                                                                       resources.end());
    request_.mutable_resource_names()->Swap(&resources_vector);
    sendDiscoveryRequest();
  }

  // Grpc::AsyncStreamCallbacks
  void onCreateInitialMetadata(Http::HeaderMap& metadata) override {
    UNREFERENCED_PARAMETER(metadata);
  }

  void onReceiveInitialMetadata(Http::HeaderMapPtr&& metadata) override {
    UNREFERENCED_PARAMETER(metadata);
  }

  void onReceiveMessage(std::unique_ptr<envoy::api::v2::DiscoveryResponse>&& message) override {
    const auto typed_resources = Config::Utility::getTypedResources<ResourceType>(*message);
    try {
      callbacks_->onConfigUpdate(typed_resources);
      request_.set_version_info(ProtobufTypes::FromString(message->version_info()));
      stats_.update_success_.inc();
    } catch (const EnvoyException& e) {
      ENVOY_LOG(warn, "gRPC config update rejected: {}", e.what());
      stats_.update_rejected_.inc();
      callbacks_->onConfigUpdateFailed(&e);
    }
    // This effectively ACK/NACKs the accepted configuration.
    ENVOY_LOG(debug, "Sending version update: {}",
              ProtobufTypes::FromString(message->version_info()));
    stats_.update_attempt_.inc();
    sendDiscoveryRequest();
  }

  void onReceiveTrailingMetadata(Http::HeaderMapPtr&& metadata) override {
    UNREFERENCED_PARAMETER(metadata);
  }

  void onRemoteClose(Grpc::Status::GrpcStatus status) override {
    ENVOY_LOG(warn, "gRPC config stream closed: {}", status);
    handleFailure();
    stream_ = nullptr;
  }

  // TODO(htuch): Make this configurable or some static.
  const uint32_t RETRY_DELAY_MS = 5000;

private:
  void handleFailure() {
    stats_.update_failure_.inc();
    callbacks_->onConfigUpdateFailed(nullptr);
    setRetryTimer();
  }

  std::unique_ptr<
      Grpc::AsyncClient<envoy::api::v2::DiscoveryRequest, envoy::api::v2::DiscoveryResponse>>
      async_client_;
  const Protobuf::MethodDescriptor& service_method_;
  Event::TimerPtr retry_timer_;
  Protobuf::RepeatedPtrField<ProtobufTypes::String> resources_;
  Config::SubscriptionCallbacks<ResourceType>* callbacks_{};
  Grpc::AsyncStream<envoy::api::v2::DiscoveryRequest>* stream_{};
  envoy::api::v2::DiscoveryRequest request_;
  SubscriptionStats stats_;
};

} // namespace Config
} // namespace Envoy
