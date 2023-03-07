#pragma once

#include <functional>
#include <memory>

#include "envoy/common/random_generator.h"
#include "envoy/config/grpc_mux.h"
#include "envoy/grpc/async_client.h"

#include "source/common/common/backoff_strategy.h"
#include "source/common/common/token_bucket_impl.h"
#include "source/common/config/utility.h"
#include "source/common/grpc/typed_async_client.h"

namespace Envoy {
namespace Config {

namespace {

// TODO(htuch): Make this configurable.
constexpr uint32_t RetryInitialDelayMs = 500;
constexpr uint32_t RetryMaxDelayMs = 30000; // Do not cross more than 30s

} // namespace

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

    backoff_strategy_ = std::make_unique<JitteredExponentialBackOffStrategy>(
        RetryInitialDelayMs, RetryMaxDelayMs, random_);
  }

  void establishNewStream() {
    ENVOY_LOG(debug, "Establishing new gRPC bidi stream to {} for {}", async_client_.destination(),
              service_method_.DebugString());
    if (stream_ != nullptr) {
      ENVOY_LOG(warn, "gRPC bidi stream to {} for {} already exists!", async_client_.destination(),
                service_method_.DebugString());
      return;
    }
    stream_ = async_client_->start(service_method_, *this, Http::AsyncClient::StreamOptions());
    if (stream_ == nullptr) {
      ENVOY_LOG(debug, "Unable to establish new stream to configuration server {}",
                async_client_.destination());
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
    // Clear here instead of on stream establishment in case streams are immediately closed
    // repeatedly.
    clearCloseStatus();
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
    logClose(status, message);
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

  absl::optional<Grpc::Status::GrpcStatus> getCloseStatus() { return last_close_status_; }

private:
  void setRetryTimer() {
    retry_timer_->enableTimer(std::chrono::milliseconds(backoff_strategy_->nextBackOffMs()));
  }

  // Log level should be reduced when the remote close failure is `Ok` or is retriable and has only
  // been occurring for a short amount of time.
  void logClose(Grpc::Status::GrpcStatus status, const std::string& message) {
    if (Grpc::Status::WellKnownGrpcStatus::Ok == status) {
      ENVOY_LOG(debug, "{} gRPC config stream to {} closed: {}, {}", service_method_.name(),
                async_client_.destination(), status, message);
      return;
    }

    if (!isNonRetriableFailure(status)) {
      // When the failure is considered non-retriable, warn.
      ENVOY_LOG(warn, "{} gRPC config stream to {} closed: {}, {}", service_method_.name(),
                async_client_.destination(), status, message);
      return;
    }

    if (!isCloseStatusSet()) {
      // For the first failure, record its occurrence and log at the debug level.
      ENVOY_LOG(debug, "{} gRPC config stream to {} closed: {}, {}", service_method_.name(),
                async_client_.destination(), status, message);
      setCloseStatus(status, message);
      return;
    }

    const auto duration_since_first_close = time_source_.monotonicTime() - last_close_time_;
    const uint64_t seconds_since_first_close =
        std::chrono::duration_cast<std::chrono::seconds>(duration_since_first_close).count();
    const Grpc::Status::GrpcStatus close_status = last_close_status_.value();

    if (status != close_status) {
      // This is a different failure. Warn on both statuses and remember the new one.
      ENVOY_LOG(warn,
                "{} gRPC config stream to {} closed: {}, {} (previously {}, {} since {}s ago)",
                service_method_.name(), async_client_.destination(), status, message, close_status,
                last_close_message_, seconds_since_first_close);
      setCloseStatus(status, message);
      return;
    }

    // #18508: The error message may have changed.
    // To reduce noise, do not update the last close time, or use the message to distinguish the
    // error in the previous condition.
    last_close_message_ = message;

    const uint64_t ms_since_first_close =
        std::chrono::duration_cast<std::chrono::milliseconds>(duration_since_first_close).count();
    if (ms_since_first_close > RetryMaxDelayMs) {
      // Warn if we are over the time limit.
      ENVOY_LOG(warn, "{} gRPC config stream to {} closed since {}s ago: {}, {}",
                service_method_.name(), async_client_.destination(), seconds_since_first_close,
                close_status, message);
      return;
    }

    // Failure is retriable and new enough to only log at the debug level.
    ENVOY_LOG(debug, "{} gRPC config stream to {} closed: {}, {}", service_method_.name(),
              async_client_.destination(), status, message);
  }

  bool isNonRetriableFailure(Grpc::Status::GrpcStatus status) {
    // Status codes from https://grpc.github.io/grpc/core/md_doc_statuscodes.html that potentially
    // indicate a high likelihood of success after retrying with backoff.
    //
    // - DeadlineExceeded may be from a latency spike
    // - ResourceExhausted may be from a rate limit with a short window or a transient period of too
    //   many connections
    // - Unavailable is meant to be used for a transient downtime
    return Grpc::Status::WellKnownGrpcStatus::DeadlineExceeded == status ||
           Grpc::Status::WellKnownGrpcStatus::ResourceExhausted == status ||
           Grpc::Status::WellKnownGrpcStatus::Unavailable == status;
  }

  void clearCloseStatus() { last_close_status_ = absl::nullopt; }
  bool isCloseStatusSet() { return last_close_status_.has_value(); }

  void setCloseStatus(Grpc::Status::GrpcStatus status, const std::string& message) {
    last_close_status_ = status;
    last_close_time_ = time_source_.monotonicTime();
    last_close_message_ = message;
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

  // Records the initial message and timestamp of the most recent remote closes with the same
  // status.
  absl::optional<Grpc::Status::GrpcStatus> last_close_status_;
  std::string last_close_message_;
  MonotonicTime last_close_time_;
};

} // namespace Config
} // namespace Envoy
