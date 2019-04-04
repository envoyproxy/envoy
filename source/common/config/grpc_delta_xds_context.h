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

// (TODO this will become more of a README level thing)
// When using gRPC, xDS has two pairs of options: aggregated/non-aggregated, and
// delta/state-of-the-world updates. All four combinations of these are usable.
//
// "Aggregated" means that EDS, CDS, etc resources are all carried by the same gRPC stream (not even
// channel). For Envoy's implementation of xDS client logic, there is effectively no difference
// between aggregated xDS and non-aggregated: they both use the same request/response protos. The
// non-aggregated case is handled by running the aggregated logic, and just happening to only have 1
// xDS subscription type to "aggregate", i.e., GrpcDeltaXdsContext only has one
// DeltaSubscriptionState entry in its map. The sole implementation difference: when the bootstrap
// specifies ADS, the method string passed to gRPC is {Delta,Stream}AggregatedResources, as opposed
// to e.g. {Delta,Stream}Clusters. This distinction is necessary for the server to know what
// resources should be provided.
//
// DeltaSubscriptionState is what handles the conceptual/application-level protocol state of a given
// resource type subscription, which may or may not be multiplexed with others. So,
// DeltaSubscriptionState is equally applicable to non- and aggregated.
//
// Delta vs state-of-the-world is a question of wire format: the protos in question are named
// [Delta]Discovery{Request,Response}. That is what the XdsGrpcContext interface is useful for: its
// GrpcDeltaXdsContext implementation works with DeltaDiscovery{Request,Response} and has
// delta-specific logic, and its GrpxMuxImpl implementation works with Discovery{Request,Response}
// and has SotW-specific logic. A DeltaSubscriptionImpl (TODO rename to not delta-specific since
// it's ideally going to cover ALL 4 subscription "meta-types") has its shared_ptr<XdsGrpcContext>.
// Both GrpcDeltaXdsContext (delta) or GrpcMuxImpl (SotW) will work just fine. The shared_ptr allows
// for both non- and aggregated: if non-aggregated, you'll be the only holder of that shared_ptr. By
// those two mechanisms, the single class (TODO rename) DeltaSubscriptionImpl handles all 4
// delta/SotW and non-/aggregated combinations.
class GrpcDeltaXdsContext : public XdsGrpcContext, Logger::Loggable<Logger::Id::config> {
public:
  GrpcDeltaXdsContext(Grpc::AsyncClientPtr async_client, Event::Dispatcher& dispatcher,
                      const Protobuf::MethodDescriptor& service_method,
                      Runtime::RandomGenerator& random, Stats::Scope& scope,
                      const RateLimitSettings& rate_limit_settings,
                      const LocalInfo::LocalInfo& local_info)
      : local_info_(local_info), dispatcher_(dispatcher),
        grpc_stream_(this, std::move(async_client), service_method, random, dispatcher, scope,
                     rate_limit_settings,
                     // callback for handling receipt of DiscoveryResponse protos.
                     [this](std::unique_ptr<envoy::api::v2::DeltaDiscoveryResponse>&& message) {
                       handleDiscoveryResponse(std::move(message));
                     }) {}

  void addSubscription(const std::vector<std::string>& resources, const std::string& type_url,
                       SubscriptionCallbacks& callbacks, SubscriptionStats& stats,
                       std::chrono::milliseconds init_fetch_timeout) override {
    subscriptions_.emplace(
        std::make_pair(type_url, DeltaSubscriptionState(type_url, resources, callbacks, local_info_,
                                                        init_fetch_timeout, dispatcher_, stats)));
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
      // Due to the first_request_of_new_stream logic, this "empty" diff will actually become a
      // request populated with all the resource names passed to the DeltaSubscriptionState's
      // constructor.
      queueDiscoveryRequest(ResourceNameDiff(sub.first));
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
  Event::Dispatcher& dispatcher_;

  GrpcStream<envoy::api::v2::DeltaDiscoveryRequest, envoy::api::v2::DeltaDiscoveryResponse>
      grpc_stream_;

  // A queue to store requests while rate limited. Note that when requests cannot be sent due to the
  // gRPC stream being down, this queue does not store them; rather, they are simply dropped.
  std::queue<ResourceNameDiff> request_queue_;
  // Map from type_url strings to a DeltaSubscriptionState for that type.
  std::unordered_map<std::string, DeltaSubscriptionState> subscriptions_;
};

typedef std::shared_ptr<GrpcDeltaXdsContext> GrpcDeltaXdsContextSharedPtr;

} // namespace Config
} // namespace Envoy
