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

// Manages subscriptions to one or more type of resource. The logical protocol
// state of those subscription(s) is handled by DeltaSubscriptionState.
// This class owns the GrpcStream used to talk to the server, maintains queuing
// logic to properly order the subscription(s)' various messages, and allows
// starting/stopping/pausing of the subscriptions.
class GrpcDeltaXdsContext : public GrpcMux,
                            public GrpcStreamCallbacks<envoy::api::v2::DeltaDiscoveryResponse>,
                            Logger::Loggable<Logger::Id::config> {
public:
  GrpcDeltaXdsContext(Grpc::AsyncClientPtr async_client, Event::Dispatcher& dispatcher,
                      const Protobuf::MethodDescriptor& service_method,
                      Runtime::RandomGenerator& random, Stats::Scope& scope,
                      const RateLimitSettings& rate_limit_settings,
                      const LocalInfo::LocalInfo& local_info)
      : dispatcher_(dispatcher), local_info_(local_info),
        grpc_stream_(this, std::move(async_client), service_method, random, dispatcher, scope,
                     rate_limit_settings) {}

  void addSubscription(const std::set<std::string>& resources, const std::string& type_url,
                       SubscriptionCallbacks& callbacks, SubscriptionStats& stats,
                       std::chrono::milliseconds init_fetch_timeout) override {
    subscriptions_.emplace(type_url, std::make_unique<DeltaSubscriptionState>(
                                         type_url, resources, callbacks, local_info_,
                                         init_fetch_timeout, dispatcher_, stats));
    subscription_ordering_.emplace_back(type_url);
    grpc_stream_.establishNewStream(); // (idempotent) TODO TODO does print warning though...
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
    sub->second->updateResourceInterest(resources);
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
    sub->second->pause();
  }

  void resume(const std::string& type_url) override {
    auto sub = subscriptions_.find(type_url);
    if (sub == subscriptions_.end()) {
      ENVOY_LOG(warn, "Not resuming non-existent subscription {}.", type_url);
      return;
    }
    sub->second->resume();
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
    kickOffAck(sub->second->handleResponse(*message));
  }

  void onStreamEstablished() override {
    for (auto& sub : subscriptions_) {
      sub.second->markStreamFresh();
    }
    trySendDiscoveryRequests();
  }

  void onEstablishmentFailure() override {
    for (auto& sub : subscriptions_) {
      sub.second->handleEstablishmentFailure();
    }
  }

  void onWriteable() override { trySendDiscoveryRequests(); }

  void kickOffAck(UpdateAck ack) {
    ack_queue_.push(ack);
    trySendDiscoveryRequests();
  }

  // TODO(fredlas) remove, only here for compatibility with old-style GrpcMuxImpl.
  GrpcMuxWatchPtr subscribe(const std::string&, const std::set<std::string>&,
                            GrpcMuxCallbacks&) override {
    // don't need any implementation here. only grpc_mux_subscription_impl ever calls it, and there
    // would never be a GrpcDeltaXdsContext held by one of those.
    NOT_IMPLEMENTED_GCOVR_EXCL_LINE;
    return nullptr;
  }
  void start() override {
    // don't need any implementation here. only grpc_mux_subscription_impl ever calls it, and there
    // would never be a GrpcDeltaXdsContext held by one of those.
    NOT_IMPLEMENTED_GCOVR_EXCL_LINE;
  }

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
        // It's safe to assume the front of the ACK queue is of this type, because that's the only
        // way whoWantsToSendDiscoveryRequest() could return something for a non-existent
        // subscription.
        ack_queue_.pop();
        continue;
      }
      // Try again later if paused/rate limited/stream down.
      if (!canSendDiscoveryRequest(next_request_type_url, sub->second.get())) {
        break;
      }
      // Get our subscription state to generate the appropriate DeltaDiscoveryRequest, and send.
      if (!ack_queue_.empty()) {
        // Because ACKs take precedence over plain requests, if there is anything in the queue, it's
        // safe to assume it's what we want to send here.
        grpc_stream_.sendMessage(sub->second->getNextRequestWithAck(ack_queue_.front()));
        ack_queue_.pop();
      } else {
        grpc_stream_.sendMessage(sub->second->getNextRequestAckless());
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
  // Returns the type_url we should send the DeltaDiscoveryRequest for (if any).
  // First, prioritizes ACKs over non-ACK subscription interest updates.
  // Then, prioritizes non-ACK updates in the order the various types
  // of subscriptions were activated.
  absl::optional<std::string> whoWantsToSendDiscoveryRequest() {
    // All ACKs are sent before plain updates. trySendDiscoveryRequests() relies on this. So, choose
    // type_url from ack_queue_ if possible, before looking at pending updates.
    if (!ack_queue_.empty()) {
      return ack_queue_.front().type_url_;
    }
    // If we're looking to send multiple non-ACK requests, send them in the order that their
    // subscriptions were initiated.
    for (const auto& sub_type : subscription_ordering_) {
      auto sub = subscriptions_.find(sub_type);
      if (sub == subscriptions_.end()) {
        continue;
      }
      if (sub->second->subscriptionUpdatePending()) {
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
  absl::flat_hash_map<std::string, std::unique_ptr<DeltaSubscriptionState>> subscriptions_;

  // Determines the order of initial discovery requests. (Assumes that subscriptions are added in
  // the order of Envoy's dependency ordering).
  std::list<std::string> subscription_ordering_;

  GrpcStream<envoy::api::v2::DeltaDiscoveryRequest, envoy::api::v2::DeltaDiscoveryResponse>
      grpc_stream_;
};

typedef std::shared_ptr<GrpcDeltaXdsContext> GrpcDeltaXdsContextSharedPtr;

} // namespace Config
} // namespace Envoy
