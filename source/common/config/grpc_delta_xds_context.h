#pragma once

#include <queue>

#include "envoy/api/v2/discovery.pb.h"
#include "envoy/common/token_bucket.h"
#include "envoy/config/subscription.h"
#include "envoy/config/xds_context.h"

#include "common/common/assert.h"
#include "common/common/backoff_strategy.h"
#include "common/common/logger.h"
#include "common/common/token_bucket_impl.h"
#include "common/config/delta_subscription_state.h"
#include "common/config/grpc_stream.h"
#include "common/config/utility.h"
#include "common/grpc/common.h"
#include "common/protobuf/protobuf.h"
#include "common/protobuf/utility.h"

namespace Envoy {
namespace Config {

class GrpcDeltaXdsContext : public XdsGrpcContext, Logger::Loggable<Logger::Id::config> {
public:
  GrpcDeltaXdsContext(Grpc::AsyncClientPtr async_client, Event::Dispatcher& dispatcher,
                      const Protobuf::MethodDescriptor& service_method,
                      Runtime::RandomGenerator& random, Stats::Scope& scope,
                      const RateLimitSettings& rate_limit_settings,
                      const LocalInfo::LocalInfo& local_info,
                      std::chrono::milliseconds init_fetch_timeout)
      : local_info_(local_info), init_fetch_timeout_(init_fetch_timeout), dispatcher_(dispatcher),
        grpc_stream_(this, std::move(async_client), service_method, random, dispatcher, scope,
                     rate_limit_settings,
                     // callback for handling receipt of DiscoveryResponse protos.
                     [this](std::unique_ptr<envoy::api::v2::DeltaDiscoveryResponse>&& message) {
                       handleDiscoveryResponse(std::move(message));
                     }) {}

  void addSubscription(const std::vector<std::string>& resources, const std::string& type_url,
                       SubscriptionCallbacks& callbacks, SubscriptionStats stats) override {
    subscriptions_.emplace(
        std::make_pair(type_url, DeltaSubscriptionState(type_url, resources, callbacks, local_info_,
                                                        init_fetch_timeout_, dispatcher_, stats)));
    grpc_stream_.establishNewStream(); // (idempotent)
  }

  // Enqueues and attempts to send a discovery request, (un)subscribing to resources missing from /
  // added to the passed 'resources' argument, relative to resource_versions_.
  void updateResources(const std::vector<std::string>& resources,
                       const std::string& type_url) override {
    auto sub = subscriptions_.find(type_url);
    if (sub == subscriptions_.end()) {
      ENVOY_LOG(warn, "Not updating non-existent subscription {}.", type_url);
      return;
    }
    ResourceNameDiff diff;
    diff.type_url_ = type_url;
    sub->second.updateResourceNamesAndBuildDiff(resources, diff);
    queueDiscoveryRequest(diff);
  }

  void removeSubscription(const std::string& type_url) override { subscriptions_.erase(type_url); }

  void pause(const std::string& type_url) override {
    auto sub = subscriptions_.find(type_url);
    if (sub == subscriptions_.end()) {
      ENVOY_LOG(warn, "Not pausing non-existent subscription {}.", type_url);
      return;
    }
    sub->second.pause();
  }

  void resume(const std::string& type_url) override {
    auto sub = subscriptions_.find(type_url);
    if (sub == subscriptions_.end()) {
      ENVOY_LOG(warn, "Not resuming non-existent subscription {}.", type_url);
      return;
    }
    absl::optional<ResourceNameDiff> to_send_if_any;
    sub->second.resume(to_send_if_any);
    if (to_send_if_any.has_value()) {
      // Note that if some code pauses a certain type, then causes another discovery request to be
      // queued up for it, then the newly queued request can *replace* other requests (of the same
      // type) that were in the queue - that is, the new one will be sent upon unpause, while the
      // others are just dropped.
      queueDiscoveryRequest(to_send_if_any.value());
    }
  }

  void sendDiscoveryRequest(const ResourceNameDiff& diff) {
    auto sub = subscriptions_.find(diff.type_url_);
    if (sub == subscriptions_.end()) {
      ENVOY_LOG(warn, "Not sending DeltaDiscoveryRequest for non-existent subscription {}.",
                diff.type_url_);
      return;
    }
    if (!grpc_stream_.grpcStreamAvailable()) {
      ENVOY_LOG(debug, "No stream available to sendDiscoveryRequest for {}", diff.type_url_);
      return; // Drop this request; the reconnect will enqueue a new one.
    }
    if (sub->second.checkPausedDuringSendAttempt(diff)) {
      return;
    }
    const envoy::api::v2::DeltaDiscoveryRequest& request =
        sub->second.populateRequestWithDiff(diff);
    ENVOY_LOG(trace, "Sending DeltaDiscoveryRequest for {}: {}", diff.type_url_,
              request.DebugString());
    // NOTE: at this point we are beyond the rate-limiting logic. sendMessage() unconditionally
    // sends its argument over the gRPC stream. So, it is safe to unconditionally
    // set_first_request_of_new_stream(false).
    grpc_stream_.sendMessage(request);
    sub->second.set_first_request_of_new_stream(false);
    sub->second.buildCleanRequest();
  }

  void handleDiscoveryResponse(std::unique_ptr<envoy::api::v2::DeltaDiscoveryResponse>&& message) {
    ENVOY_LOG(debug, "Received DeltaDiscoveryResponse for {} at version {}", message->type_url(),
              message->system_version_info());
    auto sub = subscriptions_.find(message->type_url());
    if (sub == subscriptions_.end()) {
      ENVOY_LOG(warn,
                "Dropping received DeltaDiscoveryResponse at version {} for non-existent "
                "subscription {}.",
                message->system_version_info(), message->type_url());
      return;
    }
    sub->second.handleResponse(message.get());
    // Queue up an ACK.
    queueDiscoveryRequest(
        ResourceNameDiff(message->type_url())); // no change to subscribed resources
  }

  void handleStreamEstablished() override {
    // Ensure that there's nothing leftover from previous streams in the request proto queue.
    clearRequestQueue();
    for (auto& sub : subscriptions_) {
      sub.second.set_first_request_of_new_stream(true);
    }
  }

  void handleEstablishmentFailure() override {
    for (auto& sub : subscriptions_) {
      sub.second.handleEstablishmentFailure();
    }
  }

  // Request queue management logic.
  void queueDiscoveryRequest(const ResourceNameDiff& queue_item) {
    request_queue_.push(queue_item);
    drainRequests();
  }
  void clearRequestQueue() {
    grpc_stream_.maybeUpdateQueueSizeStat(0);
    // TODO(fredlas) when we have C++17: request_queue_ = {};
    while (!request_queue_.empty()) {
      request_queue_.pop();
    }
  }
  void drainRequests() override {
    ENVOY_LOG(trace, "draining discovery requests {}", request_queue_.size());
    while (!request_queue_.empty() && grpc_stream_.checkRateLimitAllowsDrain()) {
      // Process the request, if rate limiting is not enabled at all or if it is under rate limit.
      sendDiscoveryRequest(request_queue_.front());
      request_queue_.pop();
    }
    grpc_stream_.maybeUpdateQueueSizeStat(request_queue_.size());
  }

  // TODO TODO remove, GrpcMux impersonation
  virtual GrpcMuxWatchPtr subscribe(const std::string&, const std::vector<std::string>&,
                                    GrpcMuxCallbacks&) override {
    return nullptr;
  }
  virtual void start() override {}

private:
  const LocalInfo::LocalInfo& local_info_;
  const std::chrono::milliseconds init_fetch_timeout_;
  Event::Dispatcher& dispatcher_;

  GrpcStream<envoy::api::v2::DeltaDiscoveryRequest, envoy::api::v2::DeltaDiscoveryResponse>
      grpc_stream_;

  // A queue to store requests while rate limited. Note that when requests cannot be sent due to the
  // gRPC stream being down, this queue does not store them; rather, they are simply dropped.
  std::queue<ResourceNameDiff> request_queue_;
  std::unordered_map<std::string, DeltaSubscriptionState> subscriptions_;
};

typedef std::shared_ptr<GrpcDeltaXdsContext> GrpcDeltaXdsContextSharedPtr;

} // namespace Config
} // namespace Envoy
