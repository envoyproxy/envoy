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
  // added to the passed 'resources' argument, relative to resource_versions_.
  void buildAndQueueDiscoveryRequest(const std::vector<std::string>& resources) {
    ResourceNameDiff diff;
    std::set_difference(resources.begin(), resources.end(), resource_names_.begin(),
                        resource_names_.end(), std::inserter(diff.added_, diff.added_.begin()));
    std::set_difference(resource_names_.begin(), resource_names_.end(), resources.begin(),
                        resources.end(), std::inserter(diff.removed_, diff.removed_.begin()));

    for (const auto& added : diff.added_) {
      setResourceWaitingForServer(added);
    }
    for (const auto& removed : diff.removed_) {
      lostInterestInResource(removed);
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

  envoy::api::v2::DeltaDiscoveryRequest internalRequestStateForTest() const { return request_; }

  // Config::SubscriptionCallbacks
  void onConfigUpdate(const Protobuf::RepeatedPtrField<envoy::api::v2::Resource>& added_resources,
                      const Protobuf::RepeatedPtrField<std::string>& removed_resources,
                      const std::string& version_info) {
    callbacks_->onConfigUpdate(added_resources, removed_resources, version_info);
    for (const auto& resource : added_resources) {
      setResourceVersion(resource.name(), resource.version());
    }
    // If a resource is gone, there is no longer a meaningful version for it that makes sense to
    // provide to the server upon stream reconnect: either it will continue to not exist, in which
    // case saying nothing is fine, or the server will bring back something new, which we should
    // receive regardless (which is the logic that not specifying a version will get you).
    //
    // So, leave the version map entry present but blank. It will be left out of
    // initial_resource_versions messages, but will remind us to explicitly tell the server "I'm
    // cancelling my subscription" when we lose interest.
    for (const auto& resource_name : removed_resources) {
      if (resource_names_.find(resource_name) != resource_names_.end()) {
        setResourceWaitingForServer(resource_name);
      }
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
    for (auto const& resource : resource_versions_) {
      // Populate initial_resource_versions with the resource versions we currently have. Resources
      // we are interested in, but are still waiting to get any version of from the server, do not
      // belong in initial_resource_versions.
      if (!resource.second.waitingForServer()) {
        (*request_.mutable_initial_resource_versions())[resource.first] = resource.second.version();
      }
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

  class ResourceVersion {
  public:
    explicit ResourceVersion(absl::string_view version) : version_(version) {}
    // Builds a ResourceVersion in the waitingForServer state.
    ResourceVersion() {}

    // If true, we currently have no version of this resource - we are waiting for the server to
    // provide us with one.
    bool waitingForServer() const { return version_ == absl::nullopt; }
    // Must not be called if waitingForServer() == true.
    std::string version() const {
      ASSERT(version_.has_value());
      return version_.value_or("");
    }

  private:
    absl::optional<std::string> version_;
  };

  // Use these helpers to avoid forgetting to update both at once.
  void setResourceVersion(const std::string& resource_name, const std::string& resource_version) {
    resource_versions_[resource_name] = ResourceVersion(resource_version);
    resource_names_.insert(resource_name);
  }

  void setResourceWaitingForServer(const std::string& resource_name) {
    resource_versions_[resource_name] = ResourceVersion();
    resource_names_.insert(resource_name);
  }

  void lostInterestInResource(const std::string& resource_name) {
    resource_versions_.erase(resource_name);
    resource_names_.erase(resource_name);
  }

  // A map from resource name to per-resource version. The keys of this map are exactly the resource
  // names we are currently interested in. Those in the waitingForServer state currently don't have
  // any version for that resource: we need to inform the server if we lose interest in them, but we
  // also need to *not* include them in the initial_resource_versions map upon a reconnect.
  std::unordered_map<std::string, ResourceVersion> resource_versions_;
  // The keys of resource_versions_. Only tracked separately because std::map does not provide an
  // iterator into just its keys, e.g. for use in std::set_difference.
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
