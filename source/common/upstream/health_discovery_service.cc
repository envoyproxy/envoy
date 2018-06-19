#include "common/upstream/health_discovery_service.h"

#include "common/protobuf/protobuf.h"

namespace Envoy {
namespace Upstream {

HDSReporter::HDSReporter(const envoy::api::v2::core::Node& node, ClusterManager& cluster_manager,
                         Stats::Scope& scope, Grpc::AsyncClientPtr async_client,
                         Event::Dispatcher& dispatcher)
    : cm_(cluster_manager), stats_{ALL_HDS_STATS(POOL_COUNTER_PREFIX(scope, "hds_reporter."))},
      async_client_(std::move(async_client)),
      service_method_(*Protobuf::DescriptorPool::generated_pool()->FindMethodByName(
          "envoy.service.discovery.v2.HealthDiscoveryService.StreamHealthCheck")) {
  health_check_request_.mutable_node()->MergeFrom(node);
  retry_timer_ = dispatcher.createTimer([this]() -> void { establishNewStream(); });
  response_timer_ = dispatcher.createTimer([this]() -> void { sendHealthCheckRequest(); });
  establishNewStream();
}

void HDSReporter::setRetryTimer() {
  retry_timer_->enableTimer(std::chrono::milliseconds(RETRY_DELAY_MS));
}

void HDSReporter::establishNewStream() {
  ENVOY_LOG(debug, "Establishing new gRPC bidi stream for {}", service_method_.DebugString());
  stream_ = async_client_->start(service_method_, *this);
  if (stream_ == nullptr) {
    ENVOY_LOG(warn, "Unable to establish new stream");
    handleFailure();
    return;
  }

  sendHealthCheckRequest();
}

void HDSReporter::sendHealthCheckRequest() {
  ENVOY_LOG(debug, "Sending HealthCheckRequest");
  stream_->sendMessage(health_check_request_, false);
  stats_.responses_.inc();
  ENVOY_LOG(debug, "Counter responses: " + std::to_string(stats_.responses_.value()));
}

void HDSReporter::handleFailure() {
  ENVOY_LOG(warn, "Load reporter stats stream/connection failure, will retry in {} ms.",
            RETRY_DELAY_MS);
  stats_.errors_.inc();
  setRetryTimer();
}

void HDSReporter::onCreateInitialMetadata(Http::HeaderMap& metadata) {
  UNREFERENCED_PARAMETER(metadata);
}

void HDSReporter::onReceiveInitialMetadata(Http::HeaderMapPtr&& metadata) {
  UNREFERENCED_PARAMETER(metadata);
}

void HDSReporter::onReceiveMessage(
    std::unique_ptr<envoy::service::discovery::v2::HealthCheckSpecifier>&& message) {
  ENVOY_LOG(debug, "New health check response ", message->DebugString());
  stats_.requests_.inc();
  stream_->sendMessage(health_check_request_, false);
  stats_.responses_.inc();
  ENVOY_LOG(debug, "Counter requests: " + std::to_string(stats_.requests_.value()));
  ENVOY_LOG(debug, "Counter responses: " + std::to_string(stats_.responses_.value()));
}

void HDSReporter::onReceiveTrailingMetadata(Http::HeaderMapPtr&& metadata) {
  UNREFERENCED_PARAMETER(metadata);
}

void HDSReporter::onRemoteClose(Grpc::Status::GrpcStatus status, const std::string& message) {
  ENVOY_LOG(warn, "gRPC config stream closed: {}, {}", status, message);
  response_timer_->disableTimer();
  stream_ = nullptr;
  handleFailure();
}

} // namespace Upstream
} // namespace Envoy
