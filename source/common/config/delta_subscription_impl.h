#pragma once

#include <queue>

#include "envoy/api/v2/discovery.pb.h"
#include "envoy/common/token_bucket.h"
#include "envoy/config/subscription.h"

#include "common/common/assert.h"
#include "common/common/backoff_strategy.h"
#include "common/common/logger.h"
#include "common/common/token_bucket_impl.h"
#include "common/config/grpc_stream.h"
#include "common/config/utility.h"
#include "common/grpc/common.h"
#include "common/protobuf/protobuf.h"
#include "common/protobuf/utility.h"

namespace Envoy {
namespace Config {

struct ResourceNameDiff {
  std::vector<std::string> added_;
  std::vector<std::string> removed_;
};

const char EmptyVersion[] = "";

/**
 * Manages the logic of a (non-aggregated) delta xDS subscription.
 * TODO(fredlas) add aggregation support.
 */
template <class ResourceType>
class DeltaSubscriptionImpl
    : public Subscription<ResourceType>,
      public GrpcStream<envoy::api::v2::DeltaDiscoveryRequest,
                        envoy::api::v2::DeltaDiscoveryResponse, ResourceNameDiff> {
public:
  DeltaSubscriptionImpl(const LocalInfo::LocalInfo& local_info, Grpc::AsyncClientPtr async_client,
                        Event::Dispatcher& dispatcher,
                        const Protobuf::MethodDescriptor& service_method,
                        Runtime::RandomGenerator& random, Stats::Scope& scope,
                        const RateLimitSettings& rate_limit_settings, SubscriptionStats stats,
                        std::chrono::milliseconds init_fetch_timeout)
      : GrpcStream<envoy::api::v2::DeltaDiscoveryRequest, envoy::api::v2::DeltaDiscoveryResponse,
                   ResourceNameDiff>(std::move(async_client), service_method, random, dispatcher,
                                     scope, rate_limit_settings),
        type_url_(Grpc::Common::typeUrl(ResourceType().GetDescriptor()->full_name())),
        local_info_(local_info), stats_(stats), dispatcher_(dispatcher),
        init_fetch_timeout_(init_fetch_timeout) {
    request_.set_type_url(type_url_);
    request_.mutable_node()->MergeFrom(local_info_.node());
  }

  // Enqueues and attempts to send a discovery request, (un)subscribing to resources missing from /
  // added to the passed 'resources' argument, relative to resources_. Updates resources_ to
  // 'resources'.
  void buildAndQueueDiscoveryRequest(const std::vector<std::string>& resources) {
    ResourceNameDiff diff;
    std::set_difference(resources.begin(), resources.end(), resource_names_.begin(),
                        resource_names_.end(), std::inserter(diff.added_, diff.added_.begin()));
    std::set_difference(resource_names_.begin(), resource_names_.end(), resources.begin(),
                        resources.end(), std::inserter(diff.removed_, diff.removed_.begin()));

    for (const auto& added : diff.added_) {
      resources_[added] = EmptyVersion;
      resource_names_.insert(added);
    }
    for (const auto& removed : diff.removed_) {
      resources_.erase(removed);
      resource_names_.erase(removed);
    }
    queueDiscoveryRequest(diff);
  }

  void sendDiscoveryRequest(const ResourceNameDiff& diff) override {
    if (!grpcStreamAvailable()) {
      ENVOY_LOG(debug, "No stream available to sendDiscoveryRequest for {}", type_url_);
      return; // Drop this request; the reconnect will enqueue a new one.
    }
    if (paused_) {
      ENVOY_LOG(trace, "API {} paused during sendDiscoveryRequest().", type_url_);
      pending_ = diff;
      return; // The unpause will send this request.
    }

    request_.clear_resource_names_subscribe();
    request_.clear_resource_names_unsubscribe();
    std::copy(diff.added_.begin(), diff.added_.end(),
              Protobuf::RepeatedFieldBackInserter(request_.mutable_resource_names_subscribe()));
    std::copy(diff.removed_.begin(), diff.removed_.end(),
              Protobuf::RepeatedFieldBackInserter(request_.mutable_resource_names_unsubscribe()));

    ENVOY_LOG(trace, "Sending DiscoveryRequest for {}: {}", type_url_, request_.DebugString());
    sendMessage(request_);
    request_.clear_error_detail();
    request_.clear_initial_resource_versions();
  }

  void subscribe(const std::vector<std::string>& resources) {
    ENVOY_LOG(debug, "delta subscribe for " + type_url_);
    buildAndQueueDiscoveryRequest(resources);
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
    if (pending_.has_value()) {
      queueDiscoveryRequest(pending_.value());
      pending_.reset();
    }
  }

  // Config::SubscriptionCallbacks
  void onConfigUpdate(const Protobuf::RepeatedPtrField<envoy::api::v2::Resource>& added_resources,
                      const Protobuf::RepeatedPtrField<std::string>& removed_resources,
                      const std::string& version_info) {
    callbacks_->onConfigUpdate(added_resources, removed_resources, version_info);
    for (const auto& resource : added_resources) {
      resources_[resource.name()] = resource.version();
    }
    stats_.update_success_.inc();
    stats_.update_attempt_.inc();
    stats_.version_.set(HashUtil::xxHash64(version_info));
    ENVOY_LOG(debug, "Delta config for {} accepted with {} resources added, {} removed", type_url_,
              added_resources.size(), removed_resources.size());
  }

  void handleResponse(std::unique_ptr<envoy::api::v2::DeltaDiscoveryResponse>&& message) override {
    ENVOY_LOG(debug, "Received gRPC message for {} at version {}", type_url_,
              message->system_version_info());
    disableInitFetchTimeoutTimer();

    request_.set_response_nonce(message->nonce());

    try {
      onConfigUpdate(message->resources(), message->removed_resources(),
                     message->system_version_info());
    } catch (const EnvoyException& e) {
      stats_.update_rejected_.inc();
      ENVOY_LOG(warn, "delta config for {} rejected: {}", type_url_, e.what());
      stats_.update_attempt_.inc();
      callbacks_->onConfigUpdateFailed(&e);
      ::google::rpc::Status* error_detail = request_.mutable_error_detail();
      error_detail->set_code(Grpc::Status::GrpcStatus::Internal);
      error_detail->set_message(e.what());
    }
    queueDiscoveryRequest(ResourceNameDiff()); // no change to subscribed resources
  }

  void handleStreamEstablished() override {
    // initial_resource_versions "must be populated for first request in a stream", so guarantee
    // that the initial version'd request we're about to enqueue is what gets sent.
    clearRequestQueue();

    request_.Clear();
    for (auto const& resource : resources_) {
      (*request_.mutable_initial_resource_versions())[resource.first] = resource.second;
    }
    request_.set_type_url(type_url_);
    request_.mutable_node()->MergeFrom(local_info_.node());
    queueDiscoveryRequest(ResourceNameDiff()); // no change to subscribed resources
  }

  void handleEstablishmentFailure() override {
    disableInitFetchTimeoutTimer();
    stats_.update_failure_.inc();
    ENVOY_LOG(debug, "delta update for {} failed", type_url_);
    stats_.update_attempt_.inc();
    callbacks_->onConfigUpdateFailed(nullptr);
  }

  // Config::DeltaSubscription
  void start(const std::vector<std::string>& resources,
             SubscriptionCallbacks<ResourceType>& callbacks) override {
    callbacks_ = &callbacks;

    if (init_fetch_timeout_.count() > 0) {
      init_fetch_timeout_timer_ = dispatcher_.createTimer([this]() -> void {
        ENVOY_LOG(warn, "delta config: initial fetch timed out for {}", type_url_);
        callbacks_->onConfigUpdateFailed(nullptr);
      });
      init_fetch_timeout_timer_->enableTimer(init_fetch_timeout_);
    }

    establishNewStream();
    subscribe(resources);
    // The attempt stat here is maintained for the purposes of having consistency between ADS and
    // individual DeltaSubscriptions. Since ADS is push based and muxed, the notion of an
    // "attempt" for a given xDS API combined by ADS is not really that meaningful.
    stats_.update_attempt_.inc();
  }

  void updateResources(const std::vector<std::string>& resources) override {
    subscribe(resources);
    stats_.update_attempt_.inc();
  }

private:
  void disableInitFetchTimeoutTimer() {
    if (init_fetch_timeout_timer_) {
      init_fetch_timeout_timer_->disableTimer();
      init_fetch_timeout_timer_.reset();
    }
  }
  // A map from resource name to per-resource version.
  std::unordered_map<std::string, std::string> resources_;
  // The keys of resources_. Only tracked separately because std::map does not provide an iterator
  // into just its keys, e.g. for use in std::set_difference.
  std::unordered_set<std::string> resource_names_;
  const std::string type_url_;
  SubscriptionCallbacks<ResourceType>* callbacks_{};
  // In-flight or previously sent request.
  envoy::api::v2::DeltaDiscoveryRequest request_;
  // Paused via pause()?
  bool paused_{};
  absl::optional<ResourceNameDiff> pending_;

  const LocalInfo::LocalInfo& local_info_;

  SubscriptionStats stats_;
  Event::Dispatcher& dispatcher_;
  std::chrono::milliseconds init_fetch_timeout_;
  Event::TimerPtr init_fetch_timeout_timer_;
};

} // namespace Config
} // namespace Envoy
