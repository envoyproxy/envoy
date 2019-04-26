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
// specifies ADS, the method string set on the gRPC client that the XdsGrpcContext holds is
// {Delta,Stream}AggregatedResources, as opposed to e.g. {Delta,Stream}Clusters. This distinction is
// necessary for the server to know what resources should be provided.
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
class GrpcDeltaXdsContext : public XdsGrpcContext,
                            public GrpcStreamCallbacks<envoy::api::v2::DeltaDiscoveryResponse>,
                            Logger::Loggable<Logger::Id::config> {
public:
  GrpcDeltaXdsContext(Grpc::AsyncClientPtr async_client, Event::Dispatcher& dispatcher,
                      const Protobuf::MethodDescriptor& service_method,
                      Runtime::RandomGenerator& random, Stats::Scope& scope,
                      const RateLimitSettings& rate_limit_settings,
                      const LocalInfo::LocalInfo& local_info)
      : local_info_(local_info), dispatcher_(dispatcher),
        grpc_stream_(this, std::move(async_client), service_method, random, dispatcher, scope,
                     rate_limit_settings) {}

  void addSubscription(const std::vector<std::string>& resources, const std::string& type_url,
                       SubscriptionCallbacks& callbacks, SubscriptionStats& stats,
                       std::chrono::milliseconds init_fetch_timeout) override {
    std::set<std::string> HACK_resources_as_set(resources.begin(), resources.end());
    subscriptions_.emplace(std::make_pair(
        type_url, DeltaSubscriptionState(type_url, HACK_resources_as_set, callbacks, local_info_,
                                         init_fetch_timeout, dispatcher_, stats)));
    grpc_stream_.establishStream(); // (idempotent)
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
    std::set<std::string> HACK_resources_as_set(resources.begin(), resources.end());
    if (sub->second.updateResourceInterest(
            HACK_resources_as_set)) { // any changes worth mentioning?
      kickOffDiscoveryRequest(type_url);
    }
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
    sub->second.resume();
    trySendDiscoveryRequests();
  }

  void
  onDiscoveryResponse(std::unique_ptr<envoy::api::v2::DeltaDiscoveryResponse>&& message) override {
    ENVOY_LOG(debug, "Received DeltaDiscoveryResponse for {} at version {}", message->type_url(),
              message->system_version_info());
    auto sub = subscriptions_.find(message->type_url());
    if (sub == subscriptions_.end()) {
      ENVOY_LOG(warn,
                "Dropping received DeltaDiscoveryResponse (with version {}) for non-existent "
                "subscription {}.",
                message->system_version_info(), message->type_url());
      return;
    }
    kickOffDiscoveryRequestWithAck(message->type_url(), sub->second.handleResponse(message.get()));
  }

  void onStreamEstablished() override {
    for (auto& sub : subscriptions_) {
      sub.second.set_first_request_of_new_stream(true);
      kickOffDiscoveryRequest(sub.first);
    }
  }

  void onEstablishmentFailure() override {
    for (auto& sub : subscriptions_) {
      sub.second.handleEstablishmentFailure();
    }
  }

  void onWriteable() override { trySendDiscoveryRequests(); }

  void kickOffDiscoveryRequest(const std::string& type_url) {
    kickOffDiscoveryRequestWithAck(type_url, absl::nullopt);
  }

  void kickOffDiscoveryRequestWithAck(const std::string& type_url, absl::optional<UpdateAck> ack) {
    request_queue_.push(PendingRequest(type_url, ack));
    trySendDiscoveryRequests();
  }

  // TODO TODO remove, GrpcMux impersonation
  virtual GrpcMuxWatchPtr subscribe(const std::string&, const std::vector<std::string>&,
                                    GrpcMuxCallbacks&) override {
    return nullptr;
  }

  GrpcStream<envoy::api::v2::DeltaDiscoveryRequest, envoy::api::v2::DeltaDiscoveryResponse>&
  grpcStreamForTest() {
    return grpc_stream_;
  }

private:
  void trySendDiscoveryRequests() {
    while (!request_queue_.empty()) {
      const PendingRequest& pending_request = request_queue_.front();
      auto sub = subscriptions_.find(pending_request.type_url_);
      if (sub == subscriptions_.end()) {
        ENVOY_LOG(warn, "Not sending queued request for non-existent subscription {}.",
                  pending_request.type_url_);
        request_queue_.pop();
        continue;
      }
      if (shouldSendDiscoveryRequest(sub->first, &sub->second)) {
        // TODO TODO hmm maybe should pass the ack into getNextRequest, to accomodate the
        // templatization goal.
        envoy::api::v2::DeltaDiscoveryRequest request = sub->second.getNextRequest();
        if (pending_request.ack_.has_value()) {
          const UpdateAck& ack = pending_request.ack_.value();
          request.set_response_nonce(ack.nonce_);
          request.mutable_error_detail()->CopyFrom(ack.error_detail_);
        }
        grpc_stream_.sendMessage(request);
        request_queue_.pop();
      } else {
        break;
      }
    }
    grpc_stream_.maybeUpdateQueueSizeStat(request_queue_.size());
  }

  bool shouldSendDiscoveryRequest(absl::string_view type_url, DeltaSubscriptionState* sub) {
    if (sub->paused()) {
      ENVOY_LOG(trace, "API {} paused; discovery request on hold for now.", type_url);
      return false;
    } else if (!grpc_stream_.grpcStreamAvailable()) {
      ENVOY_LOG(trace, "No stream available to send a DiscoveryRequest for {}.", type_url);
      return false;
    } else if (!grpc_stream_.checkRateLimitAllowsDrain()) {
      ENVOY_LOG(trace, "{} DiscoveryRequest hit rate limit; will try later.", type_url);
      return false;
    }
    return true;
  }

  const LocalInfo::LocalInfo& local_info_;
  Event::Dispatcher& dispatcher_;

  GrpcStream<envoy::api::v2::DeltaDiscoveryRequest, envoy::api::v2::DeltaDiscoveryResponse>
      grpc_stream_;

  struct PendingRequest {
    PendingRequest(absl::string_view type_url, absl::optional<UpdateAck> ack)
        : type_url_(type_url), ack_(ack) {}
    const std::string type_url_;
    const absl::optional<UpdateAck> ack_;
  };

  // Discovery requests we're waiting to send, stored in the order that they should be sent in. All
  // of our different API types are mixed together in this queue. Those that are ACKs have the ack_
  // field filled.
  std::queue<PendingRequest> request_queue_;

  // Map from type_url strings to a DeltaSubscriptionState for that type.
  std::unordered_map<std::string, DeltaSubscriptionState> subscriptions_;
};

typedef std::shared_ptr<GrpcDeltaXdsContext> GrpcDeltaXdsContextSharedPtr;

} // namespace Config
} // namespace Envoy
