#pragma once

#include <queue>

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
// specifies ADS, the method string set on the gRPC client that the GrpcMux holds is
// {Delta,Stream}AggregatedResources, as opposed to e.g. {Delta,Stream}Clusters. This distinction is
// necessary for the server to know what resources should be provided.
//
// DeltaSubscriptionState is what handles the conceptual/application-level protocol state of a given
// resource type subscription, which may or may not be multiplexed with others. So,
// DeltaSubscriptionState is equally applicable to non- and aggregated.
//
// Delta vs state-of-the-world is a question of wire format: the protos in question are named
// [Delta]Discovery{Request,Response}. That is what the GrpcMux interface is useful for: its
// GrpcDeltaXdsContext implementation works with DeltaDiscovery{Request,Response} and has
// delta-specific logic, and its GrpxMuxImpl implementation works with Discovery{Request,Response}
// and has SotW-specific logic. A DeltaSubscriptionImpl (TODO rename to not delta-specific since
// it's ideally going to cover ALL 4 subscription "meta-types") has its shared_ptr<GrpcMux>.
// Both GrpcDeltaXdsContext (delta) or GrpcMuxImpl (SotW) will work just fine. The shared_ptr allows
// for both non- and aggregated: if non-aggregated, you'll be the only holder of that shared_ptr. By
// those two mechanisms, the single class (TODO rename) DeltaSubscriptionImpl handles all 4
// delta/SotW and non-/aggregated combinations.
class GrpcXdsContext : public GrpcMux,
                            public GrpcStreamCallbacks<envoy::api::v2::DeltaDiscoveryResponse>,
                            Logger::Loggable<Logger::Id::config> {
public:
  void addSubscription(const std::set<std::string>& resources, const std::string& type_url,
                       SubscriptionCallbacks& callbacks, SubscriptionStats& stats,
                       std::chrono::milliseconds init_fetch_timeout) override {
    subscriptions_.emplace(std::make_pair(
        type_url, subscription_factory_.makeSubscriptionState(type_url, resources, callbacks, local_info_,
                                         init_fetch_timeout, dispatcher_, stats)));
    subscription_ordering_.emplace_back(type_url);
    establishStream(); // (idempotent)
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
    sub->second.updateResourceInterest(resources);
    // Tell the server about our new interests, if there are any.
    trySendDiscoveryRequests();
  }

  void removeSubscription(const std::string& type_url) override {
    subscriptions_.erase(type_url); 
    // And remove from the subscription_ordering_ list.
    auto it = subscription_ordering_.begin();
    while (it != subscription_ordering_.end()) {
      if (*it == type_url) {
        it = subscription_ordering_.erase(it);
      } else {
        ++it;
      }
    }
  }

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

  // TODO TODO remove, GrpcMux impersonation. Basically combination of  addSubscription and updateResources.
  virtual GrpcMuxWatchPtr subscribe(const std::string&,
                                    const std::set<std::string>& ,
                                    GrpcMuxCallbacks& ) override {
// don't need any implementation here. only grpc_mux_subscription_impl ever calls it, and there would never be a GrpcDeltaXdsContext held by one of those.
    NOT_IMPLEMENTED_GCOVR_EXCL_LINE;
    return nullptr;
  }
  
protected:
  GrpcXdsContext(Event::Dispatcher& dispatcher, const LocalInfo::LocalInfo& local_info)
      : dispatcher_(dispatcher), local_info_(local_info) {}

  // Everything related to GrpcStream must remain abstract. GrpcStream (and the gRPC-using classes that underlie it) are templated on protobufs. That means that a single implementation that supports different types of protobufs cannot use polymorphism to share code. The workaround: the GrpcStream will be owned by a derived class, and all code that would touch grpc_stream_ is seen here in the base class as abstract.
  virtual void establishStream() PURE;
  virtual void sendDiscoveryRequestWithAck(SubscriptionState* sub, UpdateAck ack) PURE;
  virtual void sendDiscoveryRequestAckless(SubscriptionState* sub) PURE;
  virtual void maybeUpdateQueueSizeStat(uint64_t size) PURE;
  virtual bool grpcStreamAvailable() const PURE;
  virtual bool checkRateLimitAllowsDrain() PURE;

private:
  void trySendDiscoveryRequests() {
    while (true) {
      // Do any of our subscriptions even want to send a request?
      absl::optional<std::string> maybe_request_type = whoWantsToSendDiscoveryRequest();
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
        // It's safe to assume the front of the ACK queue is of this type, because that's the only way whoWantsToSendDiscoveryRequest() could return something for a non-existent subscription.
        ack_queue_.pop();
        continue;
      }
      // Try again later if paused/rate limited/stream down.
      if (!canSendDiscoveryRequest(next_request_type_url, &sub->second)) {
        break;
      }
      // Get our subscription state to generate the appropriate DeltaDiscoveryRequest, and send.
      if (!ack_queue_.empty()) {
        // Because ACKs take precedence over plain requests, if there is anything in the queue, it's safe to assume it's what we want to send here.
        sendDiscoveryRequestWithAck(&sub->second, ack_queue_.front());
        ack_queue_.pop();
      } else {
        sendDiscoveryRequestAckless(&sub->second);
      }
    }
    maybeUpdateQueueSizeStat(ack_queue_.size());
  }

  // Checks whether external conditions allow sending a DeltaDiscoveryRequest. (Does not check
  // whether we *want* to send a DeltaDiscoveryRequest).
  bool canSendDiscoveryRequest(absl::string_view type_url, DeltaSubscriptionState* sub) {
    if (sub->paused()) {
      ENVOY_LOG(trace, "API {} paused; discovery request on hold for now.", type_url);
      return false;
    } else if (!grpcStreamAvailable()) {
      ENVOY_LOG(trace, "No stream available to send a discovery request for {}.", type_url);
      return false;
    } else if (!checkRateLimitAllowsDrain()) {
      ENVOY_LOG(trace, "{} discovery request hit rate limit; will try later.", type_url);
      return false;
    }
    return true;
  }

  // Checks whether we have something to say in a DeltaDiscoveryRequest, which can be an ACK and/or
  // a subscription update. (Does not check whether we *can* send that DeltaDiscoveryRequest).
  // Returns the type_url of the resource type we should send the DeltaDiscoveryRequest for (if
  // any). Prioritizes ACKs over non-ACK subscription interest updates.
  absl::optional<std::string> whoWantsToSendDiscoveryRequest() {
    // All ACKs are sent before plain updates. trySendDiscoveryRequests() relies on this. So, choose
    // type_url from ack_queue_ if possible, before looking at pending updates.
    if (!ack_queue_.empty()) {
      return ack_queue_.front().type_url_;
    }
    // If we're looking to send multiple non-ACK requests, send them in the order that their subscriptions were initiated.
    for (const auto& sub_type : subscription_ordering_) {
      auto sub = subscriptions_.find(sub_type);
      if (sub == subscriptions_.end()) {
        continue;
      }
      if (sub->second.subscriptionUpdatePending()) {
        return sub->first;
      }
    }
    return absl::nullopt;
  }

  Event::Dispatcher& dispatcher_;
  const LocalInfo::LocalInfo& local_info_;

  // Resource (N)ACKs we're waiting to send, stored in the order that they should be sent in. All
  // of our different resource types' ACKs are mixed together in this queue.
  std::queue<UpdateAck> ack_queue_;

  // Map from type_url strings to a DeltaSubscriptionState for that type.
  std::unordered_map<std::string, DeltaSubscriptionState> subscriptions_;
  SubscriptionStateFactory subscription_factory_;
    
  // Determines the order of initial discovery requests. (Assumes that subscriptions are added in the order of Envoy's dependency ordering).
  std::list<std::string> subscription_ordering_;
};

class GrpcDeltaXdsContext : public GrpcXdsContext {
public:
  GrpcDeltaXdsContext(Grpc::AsyncClientPtr async_client, Event::Dispatcher& dispatcher,
                      const Protobuf::MethodDescriptor& service_method,
                      Runtime::RandomGenerator& random, Stats::Scope& scope,
                      const RateLimitSettings& rate_limit_settings,
                      const LocalInfo::LocalInfo& local_info) 
  : GrpcXdsContext(dispatcher, local_info),
  grpc_stream_(this, std::move(async_client), service_method, random, dispatcher, scope,
                rate_limit_settings) {}

protected:
  void establishStream() override {
    grpc_stream_.establishStream();
  }
  void sendDiscoveryRequestWithAck(SubscriptionState* sub, UpdateAck ack) override {
    grpc_stream_.sendMessage(static_cast<DeltaSubscriptionState*>(sub)->getNextRequestWithAck(ack));
  }
  void sendDiscoveryRequestAckless(SubscriptionState* sub) override {
    grpc_stream_.sendMessage(static_cast<DeltaSubscriptionState*>(sub)->getNextRequestAckless());
  }
  void maybeUpdateQueueSizeStat(uint64_t size) override {
    grpc_stream_.maybeUpdateQueueSizeStat(size);
  }
  bool grpcStreamAvailable() const override {
    return grpc_stream_.grpcStreamAvailable();
  }
  bool checkRateLimitAllowsDrain() override {
    return grpc_stream_.checkRateLimitAllowsDrain();
  }
private:
  GrpcStream<envoy::api::v2::DeltaDiscoveryRequest, envoy::api::v2::DeltaDiscoveryResponse>
      grpc_stream_;
};

typedef std::shared_ptr<GrpcDeltaXdsContext> GrpcDeltaXdsContextSharedPtr;

} // namespace Config
} // namespace Envoy
