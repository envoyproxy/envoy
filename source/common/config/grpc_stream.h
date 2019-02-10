#pragma once

#include <functional>
#include <queue>

#include "envoy/grpc/async_client.h"

#include "common/common/backoff_strategy.h"
#include "common/common/token_bucket_impl.h"
#include "common/config/utility.h"

namespace Envoy {
namespace Config {

// Oversees communication for gRPC xDS implementations (parent to both regular xDS and incremental
// xDS variants). Reestablishes the gRPC channel when necessary, and provides rate limiting of
// requests.
template <class RequestProto, class ResponseProto, class RequestQueueItem>
class GrpcStream : public Grpc::TypedAsyncStreamCallbacks<ResponseProto>,
                   public Logger::Loggable<Logger::Id::config> {
public:
  GrpcStream(Grpc::AsyncClientPtr async_client, const Protobuf::MethodDescriptor& service_method,
             Runtime::RandomGenerator& random, Event::Dispatcher& dispatcher, Stats::Scope& scope,
             const RateLimitSettings& rate_limit_settings)
      : async_client_(std::move(async_client)), service_method_(service_method),
        control_plane_stats_(generateControlPlaneStats(scope)), random_(random),
        time_source_(dispatcher.timeSource()),
        rate_limiting_enabled_(rate_limit_settings.enabled_) {
    retry_timer_ = dispatcher.createTimer([this]() -> void { establishNewStream(); });
    if (rate_limiting_enabled_) {
      // Default Bucket contains 100 tokens maximum and refills at 10 tokens/sec.
      limit_request_ = std::make_unique<TokenBucketImpl>(
          rate_limit_settings.max_tokens_, time_source_, rate_limit_settings.fill_rate_);
      drain_request_timer_ = dispatcher.createTimer([this]() { drainRequests(); });
    }
    backoff_strategy_ = std::make_unique<JitteredBackOffStrategy>(RETRY_INITIAL_DELAY_MS,
                                                                  RETRY_MAX_DELAY_MS, random_);
  }

  virtual void handleResponse(std::unique_ptr<ResponseProto>&& message) PURE;
  virtual void handleStreamEstablished() PURE;
  virtual void handleEstablishmentFailure() PURE;

  // Returns whether the request was actually sent (and so can leave the queue).
  virtual void sendDiscoveryRequest(const RequestQueueItem& queue_item) PURE;

  void queueDiscoveryRequest(const RequestQueueItem& queue_item) {
    request_queue_.push(queue_item);
    drainRequests();
  }

  void establishNewStream() {
    ENVOY_LOG(debug, "Establishing new gRPC bidi stream for {}", service_method_.DebugString());
    stream_ = async_client_->start(service_method_, *this);
    if (stream_ == nullptr) {
      ENVOY_LOG(warn, "Unable to establish new stream");
      handleEstablishmentFailure();
      setRetryTimer();
      return;
    }
    control_plane_stats_.connected_state_.set(1);
    handleStreamEstablished();
  }

  bool grpcStreamAvailable() const { return stream_ != nullptr; }

  bool checkRateLimitAllowsDrain(int queue_size) {
    if (!rate_limiting_enabled_ || limit_request_->consume()) {
      return true;
    }
    ASSERT(drain_request_timer_ != nullptr);
    control_plane_stats_.rate_limit_enforced_.inc();
    control_plane_stats_.pending_requests_.set(queue_size);
    // Enable the drain request timer.
    drain_request_timer_->enableTimer(
        std::chrono::milliseconds(limit_request_->nextTokenAvailableMs()));
    return false;
  }

  void sendMessage(const RequestProto& request) { stream_->sendMessage(request, false); }

  // Grpc::AsyncStreamCallbacks
  void onCreateInitialMetadata(Http::HeaderMap& metadata) override {
    UNREFERENCED_PARAMETER(metadata);
  }

  void onReceiveInitialMetadata(Http::HeaderMapPtr&& metadata) override {
    UNREFERENCED_PARAMETER(metadata);
  }

  void onReceiveMessage(std::unique_ptr<ResponseProto>&& message) override {
    // Reset here so that it starts with fresh backoff interval on next disconnect.
    backoff_strategy_->reset();
    handleResponse(std::move(message));
  }

  void onReceiveTrailingMetadata(Http::HeaderMapPtr&& metadata) override {
    UNREFERENCED_PARAMETER(metadata);
  }

  void onRemoteClose(Grpc::Status::GrpcStatus status, const std::string& message) override {
    ENVOY_LOG(warn, "gRPC config stream closed: {}, {}", status, message);
    stream_ = nullptr;
    control_plane_stats_.connected_state_.set(0);
    handleEstablishmentFailure();
    setRetryTimer();
  }

private:
  void drainRequests() {
    ENVOY_LOG(trace, "draining discovery requests {}", request_queue_.size());
    while (!request_queue_.empty() && checkRateLimitAllowsDrain(request_queue_.size())) {
      // Process the request, if rate limiting is not enabled at all or if it is under rate limit.
      sendDiscoveryRequest(request_queue_.front());
      request_queue_.pop();
    }
  }

  void setRetryTimer() {
    retry_timer_->enableTimer(std::chrono::milliseconds(backoff_strategy_->nextBackOffMs()));
  }

  ControlPlaneStats generateControlPlaneStats(Stats::Scope& scope) {
    const std::string control_plane_prefix = "control_plane.";
    return {ALL_CONTROL_PLANE_STATS(POOL_COUNTER_PREFIX(scope, control_plane_prefix),
                                    POOL_GAUGE_PREFIX(scope, control_plane_prefix))};
  }

  // TODO(htuch): Make this configurable or some static.
  const uint32_t RETRY_INITIAL_DELAY_MS = 500;
  const uint32_t RETRY_MAX_DELAY_MS = 30000; // Do not cross more than 30s

  std::queue<RequestQueueItem> request_queue_;

  Grpc::AsyncClientPtr async_client_;
  Grpc::AsyncStream* stream_{};
  const Protobuf::MethodDescriptor& service_method_;
  ControlPlaneStats control_plane_stats_;

  // Reestablishes the gRPC channel when necessary, with some backoff politeness.
  Event::TimerPtr retry_timer_;
  Runtime::RandomGenerator& random_;
  TimeSource& time_source_;
  BackOffStrategyPtr backoff_strategy_;

  // Prevents the Envoy from making too many requests.
  TokenBucketPtr limit_request_;
  const bool rate_limiting_enabled_;
  Event::TimerPtr drain_request_timer_;
};

} // namespace Config
} // namespace Envoy
