#pragma once

#include <queue>

#include "envoy/api/v2/discovery.pb.h"
#include "envoy/common/token_bucket.h"
#include "envoy/config/subscription.h"
#include "envoy/config/xds_grpc_context.h"

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

struct UpdateAck {
  UpdateAck(absl::string_view nonce) : nonce_(nonce) {}
  std::string nonce_;
  ::google::rpc::Status error_detail_;
};

/**
 * Manages the logic of a (non-aggregated) delta xDS subscription.
 * TODO(fredlas) add aggregation support. The plan is for that to happen in XdsGrpcContext,
 *               which this class will then "have a" rather than "be a".
 */
class DeltaSubscriptionImpl : public Subscription,
                              public GrpcStreamCallbacks<envoy::api::v2::DeltaDiscoveryResponse>,
                              public Logger::Loggable<Logger::Id::config> {
public:
  DeltaSubscriptionImpl(const LocalInfo::LocalInfo& local_info, Grpc::AsyncClientPtr async_client,
                        Event::Dispatcher& dispatcher,
                        const Protobuf::MethodDescriptor& service_method,
                        absl::string_view type_url, Runtime::RandomGenerator& random,
                        Stats::Scope& scope, const RateLimitSettings& rate_limit_settings,
                        SubscriptionStats stats, std::chrono::milliseconds init_fetch_timeout)
      : grpc_stream_(this, std::move(async_client), service_method, random, dispatcher, scope,
                     rate_limit_settings),
        type_url_(type_url), local_info_(local_info), stats_(stats), dispatcher_(dispatcher),
        init_fetch_timeout_(init_fetch_timeout) {
    request_.set_type_url(type_url_);
    request_.mutable_node()->MergeFrom(local_info_.node());
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
    trySendDiscoveryRequests();
  }

  envoy::api::v2::DeltaDiscoveryRequest internalRequestStateForTest() const { return request_; }

  // Config::Subscription
  void start(const std::set<std::string>& resources, SubscriptionCallbacks& callbacks) override {
    callbacks_ = &callbacks;

    if (init_fetch_timeout_.count() > 0) {
      init_fetch_timeout_timer_ = dispatcher_.createTimer([this]() -> void {
        ENVOY_LOG(warn, "delta config: initial fetch timed out for {}", type_url_);
        callbacks_->onConfigUpdateFailed(nullptr);
      });
      init_fetch_timeout_timer_->enableTimer(init_fetch_timeout_);
    }

    grpc_stream_.establishNewStream();
    updateResources(resources);
  }

  void updateResources(const std::set<std::string>& update_to_these_names) override {
    std::vector<std::string> cur_added;
    std::vector<std::string> cur_removed;

    std::set_difference(update_to_these_names.begin(), update_to_these_names.end(),
                        resource_names_.begin(), resource_names_.end(),
                        std::inserter(cur_added, cur_added.begin()));
    std::set_difference(resource_names_.begin(), resource_names_.end(),
                        update_to_these_names.begin(), update_to_these_names.end(),
                        std::inserter(cur_removed, cur_removed.begin()));

    for (const auto& a : cur_added) {
      setResourceWaitingForServer(a);
      // Removed->added requires us to keep track of it as a "new" addition, since our user may have
      // forgotten its copy of the resource after instructing us to remove it, and so needs to be
      // reminded of it.
      names_removed_.erase(a);
      names_added_.insert(a);
    }
    for (const auto& r : cur_removed) {
      lostInterestInResource(r);
      // Ideally, when a resource is added-then-removed in between requests, we would avoid putting
      // a superfluous "unsubscribe [resource that was never subscribed]" in the request. However,
      // the removed-then-added case *does* need to go in the request, and due to how we accomplish
      // that, it's difficult to distinguish remove-add-remove from add-remove (because "remove-add"
      // has to be treated as equivalent to just "add").
      names_added_.erase(r);
      names_removed_.insert(r);
    }

    stats_.update_attempt_.inc();
    // Tell the server about our new interests (but only if there are any).
    if (!names_added_.empty() || !names_removed_.empty()) {
      kickOffDiscoveryRequest();
    }
  }

  // Config::GrpcStreamCallbacks
  void onStreamEstablished() override {
    request_.Clear();
    for (auto const& resource : resource_versions_) {
      // Populate initial_resource_versions with the resource versions we currently have. Resources
      // we are interested in, but are still waiting to get any version of from the server, do not
      // belong in initial_resource_versions. (But do belong in new subscriptions!)
      if (!resource.second.waitingForServer()) {
        (*request_.mutable_initial_resource_versions())[resource.first] = resource.second.version();
      }
      // As mentioned above, fill resource_names_subscribe with everything.
      names_added_.insert(resource.first);
    }
    names_removed_.clear();
    request_.set_type_url(type_url_);
    request_.mutable_node()->MergeFrom(local_info_.node());
    kickOffDiscoveryRequest();
  }

  void onEstablishmentFailure() override {
    disableInitFetchTimeoutTimer();
    stats_.update_failure_.inc();
    ENVOY_LOG(debug, "delta update for {} failed", type_url_);
    // TODO(fredlas) this increment is needed to pass existing tests, but it seems wrong. We already
    // increment it when updating subscription interest, which attempts a request. Is this supposed
    // to be the sum of client- and server- initiated update attempts? Seems weird.
    stats_.update_attempt_.inc();
    callbacks_->onConfigUpdateFailed(nullptr);
  }

  void
  onDiscoveryResponse(std::unique_ptr<envoy::api::v2::DeltaDiscoveryResponse>&& message) override {
    ENVOY_LOG(debug, "Received gRPC message for {} at version {}", type_url_,
              message->system_version_info());
    disableInitFetchTimeoutTimer();

    UpdateAck ack(message->nonce());
    try {
      handleConfigUpdate(message->resources(), message->removed_resources(),
                         message->system_version_info());
    } catch (const EnvoyException& e) {
      stats_.update_rejected_.inc();
      ENVOY_LOG(warn, "delta config for {} rejected: {}", type_url_, e.what());
      // TODO(fredlas) this increment is needed to pass existing tests, but it seems wrong. We
      // already increment it when updating subscription interest, which attempts a request. Is this
      // supposed to be the sum of client- and server- initiated update attempts? Seems weird.
      stats_.update_attempt_.inc();
      callbacks_->onConfigUpdateFailed(&e);
      ack.error_detail_.set_code(Grpc::Status::GrpcStatus::Internal);
      ack.error_detail_.set_message(e.what());
    }
    kickOffDiscoveryRequestWithAck(ack);
  }

  void onWriteable() override { trySendDiscoveryRequests(); }

private:
  void
  handleConfigUpdate(const Protobuf::RepeatedPtrField<envoy::api::v2::Resource>& added_resources,
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
    // TODO(fredlas) this increment is needed to pass existing tests, but it seems wrong. We already
    // increment it when updating subscription interest, which attempts a request. Is this supposed
    // to be the sum of client- and server- initiated update attempts? Seems weird.
    stats_.update_attempt_.inc();
    stats_.version_.set(HashUtil::xxHash64(version_info));
    ENVOY_LOG(debug, "Delta config for {} accepted with {} resources added, {} removed", type_url_,
              added_resources.size(), removed_resources.size());
  }

  void disableInitFetchTimeoutTimer() {
    if (init_fetch_timeout_timer_) {
      init_fetch_timeout_timer_->disableTimer();
      init_fetch_timeout_timer_.reset();
    }
  }

  void kickOffDiscoveryRequest() { kickOffDiscoveryRequestWithAck(absl::nullopt); }

  void kickOffDiscoveryRequestWithAck(absl::optional<UpdateAck> ack) {
    ack_queue_.push(ack);
    trySendDiscoveryRequests();
  }

  // What's with the optional<UpdateAck>? DeltaDiscoveryRequest plays two independent roles:
  // informing the server of what resources we're interested in, and acknowledging resources it has
  // sent us. Some requests are queued up specifically to carry ACKs, and some are queued up for
  // resource updates. Subscription changes might get included in an ACK request. In that case, the
  // pending request that the subscription change queued up does still get sent, just empty and
  // pointless. (TODO(fredlas) we would like to skip those no-op requests).
  void sendDiscoveryRequest(absl::optional<UpdateAck> maybe_ack) {
    if (maybe_ack.has_value()) {
      const UpdateAck& ack = maybe_ack.value();
      request_.set_response_nonce(ack.nonce_);
      *request_.mutable_error_detail() = ack.error_detail_;
    }
    request_.clear_resource_names_subscribe();
    request_.clear_resource_names_unsubscribe();
    std::copy(names_added_.begin(), names_added_.end(),
              Protobuf::RepeatedFieldBackInserter(request_.mutable_resource_names_subscribe()));
    std::copy(names_removed_.begin(), names_removed_.end(),
              Protobuf::RepeatedFieldBackInserter(request_.mutable_resource_names_unsubscribe()));
    names_added_.clear();
    names_removed_.clear();

    ENVOY_LOG(trace, "Sending DiscoveryRequest for {}: {}", type_url_, request_.DebugString());
    grpc_stream_.sendMessage(request_);
    request_.clear_error_detail();
    request_.clear_initial_resource_versions();
  }

  bool shouldSendDiscoveryRequest() {
    if (paused_) {
      ENVOY_LOG(trace, "API {} paused; discovery request on hold for now.", type_url_);
      return false;
    } else if (!grpc_stream_.grpcStreamAvailable()) {
      ENVOY_LOG(trace, "No stream available to send a DiscoveryRequest for {}.", type_url_);
      return false;
    } else if (!grpc_stream_.checkRateLimitAllowsDrain()) {
      ENVOY_LOG(trace, "{} DiscoveryRequest hit rate limit; will try later.", type_url_);
      return false;
    }
    return true;
  }

  void trySendDiscoveryRequests() {
    while (!ack_queue_.empty() && shouldSendDiscoveryRequest()) {
      sendDiscoveryRequest(ack_queue_.front());
      ack_queue_.pop();
    }
    grpc_stream_.maybeUpdateQueueSizeStat(ack_queue_.size());
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

  GrpcStream<envoy::api::v2::DeltaDiscoveryRequest, envoy::api::v2::DeltaDiscoveryResponse>
      grpc_stream_;

  // A map from resource name to per-resource version. The keys of this map are exactly the resource
  // names we are currently interested in. Those in the waitingForServer state currently don't have
  // any version for that resource: we need to inform the server if we lose interest in them, but we
  // also need to *not* include them in the initial_resource_versions map upon a reconnect.
  std::unordered_map<std::string, ResourceVersion> resource_versions_;
  // The keys of resource_versions_. Only tracked separately because std::map does not provide an
  // iterator into just its keys, e.g. for use in std::set_difference.
  // Must be stored sorted to work with std::set_difference.
  std::set<std::string> resource_names_;

  const std::string type_url_;
  SubscriptionCallbacks* callbacks_{};
  // The request being built for the next send.
  envoy::api::v2::DeltaDiscoveryRequest request_;
  bool paused_{};

  // An item in the queue represents a DeltaDiscoveryRequest that must be sent. If an item is not
  // empty, it is the ACK (nonce + error_detail) to set on that request. See
  // trySendDiscoveryRequests() for more details.
  std::queue<absl::optional<UpdateAck>> ack_queue_;

  // Tracking of the delta in our subscription interest since the previous DeltaDiscoveryRequest was
  // sent. Can't use unordered_set due to ordering issues in gTest expectation matching. Feel free
  // to change if you can figure out how to make it work.
  std::set<std::string> names_added_;
  std::set<std::string> names_removed_;

  const LocalInfo::LocalInfo& local_info_;
  SubscriptionStats stats_;
  Event::Dispatcher& dispatcher_;
  std::chrono::milliseconds init_fetch_timeout_;
  Event::TimerPtr init_fetch_timeout_timer_;
};

} // namespace Config
} // namespace Envoy
