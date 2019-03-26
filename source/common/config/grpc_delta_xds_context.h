#pragma once

#include "envoy/api/v2/discovery.pb.h"
#include "envoy/common/token_bucket.h"
#include "envoy/config/subscription.h"

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

class GrpcDeltaXdsContext
    : public GrpcStream<envoy::api::v2::DeltaDiscoveryRequest,
                        envoy::api::v2::DeltaDiscoveryResponse, ResourceNameDiff> {
public:
  GrpcDeltaXdsContext(Grpc::AsyncClientPtr async_client,
                      const Protobuf::MethodDescriptor& service_method,
                      Event::Dispatcher& dispatcher, Runtime::RandomGenerator& random,
                      Stats::Scope& scope, const RateLimitSettings& rate_limit_settings)
      : GrpcStream<envoy::api::v2::DeltaDiscoveryRequest, envoy::api::v2::DeltaDiscoveryResponse,
                   ResourceNameDiff>(std::move(async_client), service_method, random, dispatcher,
                                     scope, rate_limit_settings) {}

  void addSubscription(const std::vector<std::string>& resources, const std::string& type_url,
                       const LocalInfo::LocalInfo& local_info, SubscriptionCallbacks& callbacks,
                       Event::Dispatcher& dispatcher, std::chrono::milliseconds init_fetch_timeout,
                       SubscriptionStats stats) {
    subscriptions_.emplace(
        std::make_pair(type_url, DeltaSubscriptionState(type_url, resources, callbacks, local_info,
                                                        init_fetch_timeout, dispatcher, stats)));
    establishNewStream(); // (idempotent)
  }

  // Enqueues and attempts to send a discovery request, (un)subscribing to resources missing from /
  // added to the passed 'resources' argument, relative to resource_versions_.
  void updateSubscription(const std::vector<std::string>& resources, const std::string& type_url) {
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

  void removeSubscription(const std::string& type_url) { subscriptions_.erase(type_url); }

  void pause(const std::string& type_url) {
    auto sub = subscriptions_.find(type_url);
    if (sub == subscriptions_.end()) {
      ENVOY_LOG(warn, "Not pausing non-existent subscription {}.", type_url);
      return;
    }
    sub->second.pause();
  }

  void resume(const std::string& type_url) {
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

  void sendDiscoveryRequest(const ResourceNameDiff& diff) override {
    auto sub = subscriptions_.find(diff.type_url_);
    if (sub == subscriptions_.end()) {
      ENVOY_LOG(warn, "Not sending DeltaDiscoveryRequest for non-existent subscription {}.",
                diff.type_url_);
      return;
    }
    if (!grpcStreamAvailable()) {
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
    sendMessage(request);
    sub->second.set_first_request_of_new_stream(false);
    sub->second.buildCleanRequest();
  }

  void handleResponse(std::unique_ptr<envoy::api::v2::DeltaDiscoveryResponse>&& message) override {
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

private:
  std::unordered_map<std::string, DeltaSubscriptionState> subscriptions_;
};

typedef std::shared_ptr<GrpcDeltaXdsContext> GrpcDeltaXdsContextSharedPtr;

} // namespace Config
} // namespace Envoy
