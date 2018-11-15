#pragma once

#include <queue>

#include "envoy/api/v2/discovery.pb.h"
#include "envoy/common/token_bucket.h"
#include "envoy/config/grpc_mux.h" // for ControlPlaneStats
#include "envoy/config/subscription.h"

#include "common/common/assert.h"
#include "common/common/backoff_strategy.h"
#include "common/common/logger.h"
#include "common/common/token_bucket_impl.h"
#include "common/config/utility.h"
#include "common/grpc/common.h"
#include "common/protobuf/protobuf.h"
#include "common/protobuf/utility.h"

namespace Envoy {
namespace Config {

/**
 * TODO SOMETHING SOMETHING subscription. Also handles per-xDS API stats/logging.
 */
template <class ResourceType>
class IncrementalSubscriptionImpl
    : public IncrementalSubscription<ResourceType>,
      Grpc::TypedAsyncStreamCallbacks<envoy::api::v2::IncrementalDiscoveryResponse>,
      Logger::Loggable<Logger::Id::config> {
public:
  IncrementalSubscriptionImpl(const LocalInfo::LocalInfo& local_info,
                              Grpc::AsyncClientPtr async_client, Event::Dispatcher& dispatcher,
                              const Protobuf::MethodDescriptor& service_method,
                              Runtime::RandomGenerator& random,
                              const RateLimitSettings& rate_limit_settings,
                              IncrementalSubscriptionStats stats,
                              ControlPlaneStats control_plane_stats)
      : type_url_(Grpc::Common::typeUrl(ResourceType().GetDescriptor()->full_name())),
        local_info_(local_info), async_client_(std::move(async_client)),
        service_method_(service_method), rate_limiting_enabled_(rate_limit_settings.enabled_),
        stats_(stats), control_plane_stats_(control_plane_stats) {

    retry_timer_ = dispatcher.createTimer([this]() -> void { establishNewStream(); });
    if (rate_limiting_enabled_) {
      // Default Bucket contains 100 tokens maximum and refills at 10 tokens/sec.
      limit_request_ = std::make_unique<TokenBucketImpl>(
          rate_limit_settings.max_tokens_, dispatcher.timeSystem(), rate_limit_settings.fill_rate_);
      drain_request_timer_ = dispatcher.createTimer([this]() -> void { drainRequests(); });
    }
    backoff_strategy_ =
        std::make_unique<JitteredBackOffStrategy>(50,    // TODO RETRY_INITIAL_DELAY_MS,
                                                  30000, // TODO RETRY_MAX_DELAY_MS,
                                                  random);
  }

  // GRPC ACTUAL STREAM HANDLING STUFF
  void start() { establishNewStream(); }

  void setRetryTimer() {
    retry_timer_->enableTimer(std::chrono::milliseconds(backoff_strategy_->nextBackOffMs()));
  }

  void establishNewStream() {
    ENVOY_LOG(debug, "Establishing new gRPC bidi stream for {}", service_method_.DebugString());
    stream_ = async_client_->start(service_method_, *this);
    if (stream_ == nullptr) {
      ENVOY_LOG(warn, "Unable to establish new stream");
      onIncrementalConfigFailed(nullptr);
      setRetryTimer();
      return;
    }

    // "must be populated for first request in a stream"
    // TODO what exactly are these "version" strings? just going to use "0" for now.
    for (auto const& resource : resources_) {
      request_.mutable_initial_resource_versions()[resource] = "0";
    }

    control_plane_stats_.connected_state_.set(1);
    queueDiscoveryRequest();
  }

  void drainRequests() {
    ENVOY_LOG(trace, "draining discovery requests {}", request_queue_.size());
    while (!request_queue_.empty()) {
      // Process the request, if rate limiting is not enabled at all or if it is under rate limit.
      if (!rate_limiting_enabled_ || limit_request_->consume()) {
        sendDiscoveryRequest();
      } else {
        ASSERT(rate_limiting_enabled_);
        ASSERT(drain_request_timer_ != nullptr);
        control_plane_stats_.rate_limit_enforced_.inc();
        control_plane_stats_.pending_requests_.set(request_queue_.size());
        drain_request_timer_->enableTimer(
            std::chrono::milliseconds(limit_request_->nextTokenAvailableMs()));
        break;
      }
    }
  }

  // Enqueues and attempts to send a discovery request with no change to subscribed resources.
  void queueDiscoveryRequest() { request_queue_.emplace(); }
  // Enqueues and attempts to send a discovery request, (un)subscribing to resources missing from /
  // added to the passed 'resources' argument, relative to resources_. Updates resources_ to
  // 'resources'.
  void queueDiscoveryRequest(const std::vector<std::string>& resources) {
    ResourceNameDiff diff;
    std::set_difference(resources.begin(), resources.end(), resources_.begin(), resources_.end(),
                        diff.added_.begin());
    std::set_difference(resources_.begin(), resources_.end(), resources.begin(), resources.end(),
                        diff.removed_.begin());
    request_queue_.push(diff);
    resources_ = resources;
    drainRequests();
  }

  void sendDiscoveryRequest() {
    if (stream_ == nullptr) {
      // Don't immediately try to reconnect: we rely on retry_timer_ for that.
      ENVOY_LOG(debug, "No stream available to sendDiscoveryRequest for {}", type_url_);
      return;
    }
    if (paused_) {
      ENVOY_LOG(trace, "API {} paused during sendDiscoveryRequest().", type_url_);
      return;
    }

    request_.clear_resource_names_subscribe();
    request_.clear_resource_names_unsubscribe();
    const ResourceNameDiff& diff = request_queue_.front();
    std::copy(diff.added_.begin(), diff.added_.end(),
              request_.mutable_resource_names_subscribe()->begin());
    std::copy(diff.removed_.begin(), diff.removed_.end(),
              request_.mutable_resource_names_unsubscribe()->begin());

    ENVOY_LOG(trace, "Sending DiscoveryRequest for {}: {}", type_url_, request_.DebugString());
    stream_->sendMessage(request_, false);
    request_.clear_error_detail();
    request_.clear_initial_resource_versions();
    request_queue_.pop();
  }

  void subscribe(const std::vector<std::string>& resources) {
    ENVOY_LOG(debug, "incremental subscribe for " + type_url_);

    // Lazily kick off the requests based on first subscription. This has the
    // convenient side-effect that we order messages on the channel based on
    // Envoy's internal dependency ordering.
    if (!subscribed_) {
      request_.set_type_url(type_url_);
      request_.mutable_node()->MergeFrom(local_info_.node());
      subscribed_ = true;
    }
    queueDiscoveryRequest(resources);
  }

  void pause() {
    ENVOY_LOG(debug, "Pausing discovery requests for {}", type_url_);
    ASSERT(!paused_);
    paused_ = true;
  }

  void resume() {
    ENVOY_LOG(debug, "Resuming discovery requests for {}", type_url_);
    ASSERT(paused_);
    paused_ = false;
    drainRequests();
  }

  // Config::IncrementalSubscription....Callbacks? these are just meant as wrappers. Not sure if
  // this class would have these called on it, but perhaps.
  void
  onIncrementalConfig(const Protobuf::RepeatedPtrField<envoy::api::v2::Resource>& added_resources,
                      const Protobuf::RepeatedPtrField<std::string>& removed_resources,
                      const std::string& version_info) {
    callbacks_->onIncrementalConfig(added_resources, removed_resources, version_info);
    stats_.update_success_.inc();
    stats_.update_attempt_.inc();
    stats_.version_.set(HashUtil::xxHash64(version_info));
    ENVOY_LOG(debug, "Incremental config for {} accepted with {} resources added, {} removed",
              type_url_, added_resources.size(), removed_resources.size());
  }
  void onIncrementalConfigFailed(const EnvoyException* e) {
    // TODO(htuch): Less fragile signal that this is failure vs. reject.
    if (e == nullptr) {
      stats_.update_failure_.inc();
      ENVOY_LOG(debug, "incremental update for {} failed", type_url_);
    } else {
      stats_.update_rejected_.inc();
      ENVOY_LOG(warn, "incremental config for {} rejected: {}", type_url_, e->what());
    }
    stats_.update_attempt_.inc();
    callbacks_->onIncrementalConfigFailed(e);
  }

  // Grpc::TypedAsyncStreamCallbacks
  void onCreateInitialMetadata(Http::HeaderMap& metadata) override {
    UNREFERENCED_PARAMETER(metadata);
  }
  void onReceiveInitialMetadata(Http::HeaderMapPtr&& metadata) override {
    UNREFERENCED_PARAMETER(metadata);
  }
  void onReceiveMessage(
      std::unique_ptr<envoy::api::v2::IncrementalDiscoveryResponse>&& message) override {
    // Reset here so that it starts with fresh backoff interval on next disconnect.
    backoff_strategy_->reset();

    ENVOY_LOG(debug, "Received gRPC message for {} at version {}", type_url_,
              message->system_version_info());

    request_.set_response_nonce(message->nonce());

    try {
      onIncrementalConfig(message->resources(), message->removed_resources(),
                          message->system_version_info());
    } catch (const EnvoyException& e) {
      onIncrementalConfigFailed(&e);
      ::google::rpc::Status* error_detail = request_.mutable_error_detail();
      error_detail->set_code(Grpc::Status::GrpcStatus::Internal);
      error_detail->set_message(e.what());
    }
    queueDiscoveryRequest();
  }
  void onReceiveTrailingMetadata(Http::HeaderMapPtr&& metadata) override {
    UNREFERENCED_PARAMETER(metadata);
  }
  void onRemoteClose(Grpc::Status::GrpcStatus status, const std::string& message) override {
    ENVOY_LOG(warn, "incremental config stream closed: {}, {}", status, message);
    stream_ = nullptr;
    control_plane_stats_.connected_state_.set(0);
    setRetryTimer();
  }

  // Config::IncrementalSubscription
  // TODO can combine with updateResources unless API needs to expose them both
  void start(const std::vector<std::string>& resources,
             IncrementalSubscriptionCallbacks<ResourceType>& callbacks) override {
    callbacks_ = &callbacks;

    subscribe(resources);
    // The attempt stat here is maintained for the purposes of having consistency between ADS and
    // individual IncrementalSubscriptions. Since ADS is push based and muxed, the notion of an
    // "attempt" for a given xDS API combined by ADS is not really that meaningful.
    stats_.update_attempt_.inc();
  }

  void updateResources(const std::vector<std::string>& resources) override {
    subscribe(resources);
    stats_.update_attempt_.inc();
  }

private:
  std::vector<std::string> resources_;
  const std::string type_url_;
  IncrementalSubscriptionCallbacks<ResourceType>* callbacks_{};
  // In-flight or previously sent request.
  envoy::api::v2::IncrementalDiscoveryRequest request_;
  // Paused via pause()?
  bool paused_{};
  // Has this API been tracked in subscriptions_?
  bool subscribed_{};

  const LocalInfo::LocalInfo& local_info_;
  Grpc::AsyncClientPtr async_client_;
  Grpc::AsyncStream* stream_{};
  const Protobuf::MethodDescriptor& service_method_;

  struct ResourceNameDiff {
    std::vector<std::string> added_;
    std::vector<std::string> removed_;
  };
  std::queue<ResourceNameDiff> request_queue_;
  // Detects when Envoy is making too many requests.
  TokenBucketPtr limit_request_;
  Event::TimerPtr drain_request_timer_;
  Event::TimerPtr retry_timer_;
  BackOffStrategyPtr backoff_strategy_;
  const bool rate_limiting_enabled_;

  IncrementalSubscriptionStats stats_;
  ControlPlaneStats control_plane_stats_;
};

} // namespace Config
} // namespace Envoy
