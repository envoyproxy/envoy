#pragma once

#include <functional>
#include <memory>

#include "envoy/common/random_generator.h"
#include "envoy/config/grpc_mux.h"
#include "envoy/grpc/async_client.h"

#include "common/common/backoff_strategy.h"
#include "common/common/token_bucket_impl.h"
#include "common/config/utility.h"
#include "common/grpc/typed_async_client.h"

namespace Envoy {
namespace Config {

template <class ResponseProto> using ResponseProtoPtr = std::unique_ptr<ResponseProto>;

// Oversees communication for gRPC xDS implementations (parent to both regular xDS and delta
// xDS variants). Reestablishes the gRPC channel when necessary, and provides rate limiting of
// requests.
template <class RequestProto, class ResponseProto>
class GrpcStream : public Grpc::AsyncStreamCallbacks<ResponseProto>,
                   public Logger::Loggable<Logger::Id::config> {
public:
  GrpcStream(GrpcStreamCallbacks<ResponseProto>* callbacks, Grpc::RawAsyncClientPtr async_client,
             const Protobuf::MethodDescriptor& service_method, Random::RandomGenerator& random,
             Event::Dispatcher& dispatcher, Stats::Scope& scope,
             const RateLimitSettings& rate_limit_settings)
      : callbacks_(callbacks), async_client_(std::move(async_client)),
        service_method_(service_method),
        control_plane_stats_(Utility::generateControlPlaneStats(scope)), random_(random),
        time_source_(dispatcher.timeSource()),
        rate_limiting_enabled_(rate_limit_settings.enabled_) {
    retry_timer_ = dispatcher.createTimer([this]() -> void { establishNewStream(); });
    if (rate_limiting_enabled_) {
      // Default Bucket contains 100 tokens maximum and refills at 10 tokens/sec.
      limit_request_ = std::make_unique<TokenBucketImpl>(
          rate_limit_settings.max_tokens_, time_source_, rate_limit_settings.fill_rate_);
      drain_request_timer_ = dispatcher.createTimer([this]() {
        if (stream_ != nullptr) {
          callbacks_->onWriteable();
        }
      });
    }

    // TODO(htuch): Make this configurable.
    static constexpr uint32_t RetryInitialDelayMs = 500;
    static constexpr uint32_t RetryMaxDelayMs = 30000; // Do not cross more than 30s
    backoff_strategy_ =
        std::make_unique<JitteredBackOffStrategy>(RetryInitialDelayMs, RetryMaxDelayMs, random_);
  }

  void establishNewStream() {
    ENVOY_LOG(debug, "Establishing new gRPC bidi stream for {}", service_method_.DebugString());
    if (stream_ != nullptr) {
      ENVOY_LOG(warn, "gRPC bidi stream for {} already exists!", service_method_.DebugString());
      return;
    }
    stream_ = async_client_->start(service_method_, *this, Http::AsyncClient::StreamOptions());
    if (stream_ == nullptr) {
      ENVOY_LOG(warn, "Unable to establish new stream");
      callbacks_->onEstablishmentFailure();
      setRetryTimer();
      return;
    }
    control_plane_stats_.connected_state_.set(1);
    callbacks_->onStreamEstablished();
  }

  bool grpcStreamAvailable() const { return stream_ != nullptr; }

  void sendMessage(const RequestProto& request) { stream_->sendMessage(request, false); }

  // Grpc::AsyncStreamCallbacks
  void onCreateInitialMetadata(Http::RequestHeaderMap& metadata) override {
    UNREFERENCED_PARAMETER(metadata);
  }

  void onReceiveInitialMetadata(Http::ResponseHeaderMapPtr&& metadata) override {
    UNREFERENCED_PARAMETER(metadata);
  }

  void onReceiveMessage(ResponseProtoPtr<ResponseProto>&& message) override {
    // Reset here so that it starts with fresh backoff interval on next disconnect.
    backoff_strategy_->reset();
    // Sometimes during hot restarts this stat's value becomes inconsistent and will continue to
    // have 0 until it is reconnected. Setting here ensures that it is consistent with the state of
    // management server connection.
    control_plane_stats_.connected_state_.set(1);
    callbacks_->onDiscoveryResponse(std::move(message), control_plane_stats_);
  }

  void onReceiveTrailingMetadata(Http::ResponseTrailerMapPtr&& metadata) override {
    UNREFERENCED_PARAMETER(metadata);
  }

  void onRemoteClose(Grpc::Status::GrpcStatus status, const std::string& message) override {
    ENVOY_LOG(warn, "{} gRPC config stream closed: {}, {}", service_method_.name(), status,
              message);
    stream_ = nullptr;
    control_plane_stats_.connected_state_.set(0);
    callbacks_->onEstablishmentFailure();
    setRetryTimer();
  }

  void maybeUpdateQueueSizeStat(uint64_t size) {
    // Although request_queue_.push() happens elsewhere, the only time the queue is non-transiently
    // non-empty is when it remains non-empty after a drain attempt. (The push() doesn't matter
    // because we always attempt this drain immediately after the push). Basically, a change in
    // queue length is not "meaningful" until it has persisted until here. We need the
    // if(>0 || used) to keep this stat from being wrongly marked interesting by a pointless set(0)
    // and needlessly taking up space. The first time we set(123), used becomes true, and so we will
    // subsequently always do the set (including set(0)).
    if (size > 0 || control_plane_stats_.pending_requests_.used()) {
      control_plane_stats_.pending_requests_.set(size);
    }
  }

  bool checkRateLimitAllowsDrain() {
    if (!rate_limiting_enabled_ || limit_request_->consume(1, false)) {
      return true;
    }
    ASSERT(drain_request_timer_ != nullptr);
    control_plane_stats_.rate_limit_enforced_.inc();
    // Enable the drain request timer.
    if (!drain_request_timer_->enabled()) {
      drain_request_timer_->enableTimer(limit_request_->nextTokenAvailable());
    }
    return false;
  }

private:
  void setRetryTimer() {
    retry_timer_->enableTimer(std::chrono::milliseconds(backoff_strategy_->nextBackOffMs()));
  }

  GrpcStreamCallbacks<ResponseProto>* const callbacks_;

  Grpc::AsyncClient<RequestProto, ResponseProto> async_client_;
  Grpc::AsyncStream<RequestProto> stream_{};
  const Protobuf::MethodDescriptor& service_method_;
  ControlPlaneStats control_plane_stats_;

  // Reestablishes the gRPC channel when necessary, with some backoff politeness.
  Event::TimerPtr retry_timer_;
  Random::RandomGenerator& random_;
  TimeSource& time_source_;
  BackOffStrategyPtr backoff_strategy_;

  // Prevents the Envoy from making too many requests.
  TokenBucketPtr limit_request_;
  const bool rate_limiting_enabled_;
  Event::TimerPtr drain_request_timer_;
};

} // namespace Config
} // namespace Envoy
