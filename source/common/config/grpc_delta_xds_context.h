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
        grpc_stream_(TypedGrpcStreamUnion::makeDelta(
            std::make_unique<GrpcStream<envoy::api::v2::DeltaDiscoveryRequest,
                                        envoy::api::v2::DeltaDiscoveryResponse>>(
                this, std::move(async_client), service_method, random, dispatcher, scope,
                rate_limit_settings))) {}

  void addSubscription(const std::set<std::string>& resources, const std::string& type_url,
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
  void updateResources(const std::set<std::string>& resources,
                       const std::string& type_url) override {
    auto sub = subscriptions_.find(type_url);
    if (sub == subscriptions_.end()) {
      ENVOY_LOG(warn, "Not updating non-existent subscription {}.", type_url);
      return;
    }
    std::set<std::string> HACK_resources_as_set(resources.begin(), resources.end());
    sub->second.updateResourceInterest(HACK_resources_as_set);
    // Tell the server about our new interests, if there are any.
    trySendDiscoveryRequests();
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
    kickOffAck(sub->second.handleResponse(*message));
  }

  void onStreamEstablished() override {
    for (auto& sub : subscriptions_) {
      sub.second.markStreamFresh();
    }
    trySendDiscoveryRequests();
  }

  void onEstablishmentFailure() override {
    for (auto& sub : subscriptions_) {
      sub.second.handleEstablishmentFailure();
    }
  }

  void onWriteable() override { trySendDiscoveryRequests(); }

  void kickOffAck(UpdateAck ack) {
    ack_queue_.push(ack);
    trySendDiscoveryRequests();
  }

  // TODO TODO remove, GrpcMux impersonation
  virtual GrpcMuxWatchPtr subscribe(const std::string&, const std::set<std::string>&,
                                    GrpcMuxCallbacks&) override {
    return nullptr;
  }

  //   GrpcStream<envoy::api::v2::DeltaDiscoveryRequest, envoy::api::v2::DeltaDiscoveryResponse>&
  //   grpcStreamForTest() {
  //     return grpc_stream_;
  //   }

private:
  void trySendDiscoveryRequests() {
    while (true) {
      // Do any of our subscriptions even want to send a request?
      absl::optional<std::string> maybe_request_type = wantToSendDiscoveryRequest();
      if (!maybe_request_type.has_value()) {
        break;
      }
      // If so, which one (by type_url)?
      std::string next_request_type_url = maybe_request_type.value();
      // If we don't have a subscription object for this request's type_url, drop the request.
      auto sub = subscriptions_.find(next_request_type_url);
      if (sub == subscriptions_.end()) {
        ENVOY_LOG(warn, "Not sending discovery request for non-existent subscription {}.",
                  next_request_type_url);
        ack_queue_.pop();
        continue;
      }
      // Try again later if paused/rate limited/stream down.
      if (!canSendDiscoveryRequest(next_request_type_url, &sub->second)) {
        break;
      }
      // Get our subscription state to generate the appropriate DeltaDiscoveryRequest, and send.
      if (!ack_queue_.empty()) {
        grpc_stream_.sendMessage(sub->second.getNextRequestWithAck(ack_queue_.front()));
        ack_queue_.pop();
      } else {
        grpc_stream_.sendMessage(sub->second.getNextRequestAckless());
      }
    }
    grpc_stream_.maybeUpdateQueueSizeStat(ack_queue_.size());
  }

  // Checks whether external conditions allow sending a DeltaDiscoveryRequest. (Does not check
  // whether we *want* to send a DeltaDiscoveryRequest).
  bool canSendDiscoveryRequest(absl::string_view type_url, DeltaSubscriptionState* sub) {
    if (sub->paused()) {
      ENVOY_LOG(trace, "API {} paused; discovery request on hold for now.", type_url);
      return false;
    } else if (!grpc_stream_.grpcStreamAvailable()) {
      ENVOY_LOG(trace, "No stream available to send a discovery request for {}.", type_url);
      return false;
    } else if (!grpc_stream_.checkRateLimitAllowsDrain()) {
      ENVOY_LOG(trace, "{} discovery request hit rate limit; will try later.", type_url);
      return false;
    }
    return true;
  }

  // Checks whether we have something to say in a DeltaDiscoveryRequest, which can be an ACK and/or
  // a subscription update. (Does not check whether we *can* send that DeltaDiscoveryRequest).
  // Returns the type_url of the resource type we should send the DeltaDiscoveryRequest for (if
  // any).
  absl::optional<std::string> wantToSendDiscoveryRequest() {
    // All ACKs are sent before plain updates. trySendDiscoveryRequests() relies on this. So, choose
    // type_url from ack_queue_ if possible, before looking at pending updates.
    if (!ack_queue_.empty()) {
      return ack_queue_.front().type_url_;
    }
    for (auto& sub : subscriptions_) {
      if (sub.second.subscriptionUpdatePending()) {
        return sub.first;
      }
    }
    return absl::nullopt;
  }

  const LocalInfo::LocalInfo& local_info_;
  Event::Dispatcher& dispatcher_;

  /* NOTE NOTE TODO TODO: all of this is just to support the eventual sotw+delta merger.
   * for current purposes, just sticking with this grpc_stream_ is fine:
  GrpcStream<envoy::api::v2::DeltaDiscoveryRequest, envoy::api::v2::DeltaDiscoveryResponse>
      grpc_stream_;*/
  // A horrid little class that provides polymorphism for these two protobuf types.
  class TypedGrpcStreamUnion {
  public:
    static TypedGrpcStreamUnion
    makeDelta(std::unique_ptr<GrpcStream<envoy::api::v2::DeltaDiscoveryRequest,
                                         envoy::api::v2::DeltaDiscoveryResponse>>&& delta) {
      return TypedGrpcStreamUnion(std::move(delta));
    }

    static TypedGrpcStreamUnion
    makeSotw(std::unique_ptr<GrpcStream<envoy::api::v2::DiscoveryRequest,
                                        envoy::api::v2::DiscoveryResponse>>&& sotw) {
      return TypedGrpcStreamUnion(std::move(sotw));
    }

    void establishStream() {
      delta_stream_ ? delta_stream_->establishStream() : sotw_stream_->establishStream();
    }

    bool grpcStreamAvailable() const {
      return delta_stream_ ? delta_stream_->grpcStreamAvailable()
                           : sotw_stream_->grpcStreamAvailable();
    }

    void sendMessage(const Protobuf::Message& request) {
      delta_stream_ ? delta_stream_->sendMessage(
                          static_cast<const envoy::api::v2::DeltaDiscoveryRequest&>(request))
                    : sotw_stream_->sendMessage(
                          static_cast<const envoy::api::v2::DiscoveryRequest&>(request));
    }

    /*  void onReceiveMessage(std::unique_ptr<Protobuf::Message>&& message) {
        void* ew = reinterpret_cast<void*>(message.release());
        delta_stream_
            ? delta_stream_->onReceiveMessage(
                std::unique_ptr<envoy::api::v2::DeltaDiscoveryResponse>(
                  reinterpret_cast<envoy::api::v2::DeltaDiscoveryResponse*>(ew)))
            : sotw_stream_->onReceiveMessage(
                std::unique_ptr<envoy::api::v2::DiscoveryResponse>(
                  reinterpret_cast<envoy::api::v2::DiscoveryResponse*>(ew)));
      }

      void onRemoteClose(Grpc::Status::GrpcStatus status, const std::string& message) {
        delta_stream_ ? delta_stream_->onRemoteClose(status, message)
                      : sotw_stream_->onRemoteClose(status, message);
        }*/

    void maybeUpdateQueueSizeStat(uint64_t size) {
      delta_stream_ ? delta_stream_->maybeUpdateQueueSizeStat(size)
                    : sotw_stream_->maybeUpdateQueueSizeStat(size);
    }

    bool checkRateLimitAllowsDrain() {
      return delta_stream_ ? delta_stream_->checkRateLimitAllowsDrain()
                           : sotw_stream_->checkRateLimitAllowsDrain();
    }

  private:
    explicit TypedGrpcStreamUnion(
        std::unique_ptr<
            GrpcStream<envoy::api::v2::DiscoveryRequest, envoy::api::v2::DiscoveryResponse>>&& sotw)
        : delta_stream_(nullptr), sotw_stream_(std::move(sotw)) {}

    explicit TypedGrpcStreamUnion(
        std::unique_ptr<GrpcStream<envoy::api::v2::DeltaDiscoveryRequest,
                                   envoy::api::v2::DeltaDiscoveryResponse>>&& delta)
        : delta_stream_(std::move(delta)), sotw_stream_(nullptr) {}

    std::unique_ptr<
        GrpcStream<envoy::api::v2::DeltaDiscoveryRequest, envoy::api::v2::DeltaDiscoveryResponse>>
        delta_stream_;
    std::unique_ptr<GrpcStream<envoy::api::v2::DiscoveryRequest, envoy::api::v2::DiscoveryResponse>>
        sotw_stream_;
  };
  TypedGrpcStreamUnion grpc_stream_;

  // Resource (N)ACKs we're waiting to send, stored in the order that they should be sent in. All
  // of our different resource types' ACKs are mixed together in this queue.
  std::queue<UpdateAck> ack_queue_;

  // Map from type_url strings to a DeltaSubscriptionState for that type.
  // TODO TODO to merge with SotW, State will become an interface, and this Context will need to
  // hold a factory for building these States.
  std::unordered_map<std::string, DeltaSubscriptionState> subscriptions_;
};

typedef std::shared_ptr<GrpcDeltaXdsContext> GrpcDeltaXdsContextSharedPtr;

} // namespace Config
} // namespace Envoy
