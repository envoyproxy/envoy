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
#include "common/config/pausable_ack_queue.h"
#include "common/config/utility.h"
#include "common/config/watch_map.h"
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
  GrpcDeltaXdsContext(Grpc::RawAsyncClientPtr async_client, Event::Dispatcher& dispatcher,
                      const Protobuf::MethodDescriptor& service_method,
                      Runtime::RandomGenerator& random, Stats::Scope& scope,
                      const RateLimitSettings& rate_limit_settings,
                      const LocalInfo::LocalInfo& local_info)
      : dispatcher_(dispatcher), local_info_(local_info),
        grpc_stream_(this, std::move(async_client), service_method, random, dispatcher, scope,
                     rate_limit_settings) {}

  void addOrUpdateWatch(const std::string& type_url, WatchPtr& watch,
                        const std::set<std::string>& resources, SubscriptionCallbacks& callbacks,
                        std::chrono::milliseconds init_fetch_timeout) override {
    if (watch.get() == nullptr) {
      watch = addWatch(type_url, resources, callbacks, init_fetch_timeout);
    } else {
      updateWatch(type_url, watch.get(), resources);
    }
  }

  void pause(const std::string& type_url) override { pausable_ack_queue_.pause(type_url); }

  void resume(const std::string& type_url) override {
    pausable_ack_queue_.resume(type_url);
    trySendDiscoveryRequests();
  }

  bool paused(const std::string& type_url) const override {
    return pausable_ack_queue_.paused(type_url);
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
    kickOffAck(sub->second->sub_state_.handleResponse(*message));
  }

  void onStreamEstablished() override {
    for (auto& sub : subscriptions_) {
      sub.second->sub_state_.markStreamFresh();
    }
    trySendDiscoveryRequests();
  }

  void onEstablishmentFailure() override {
    // If this happens while Envoy is still initializing, the onConfigUpdateFailed() we ultimately
    // call on CDS will cause LDS to start up, which adds to subscriptions_ here. So, to avoid a
    // crash, the iteration needs to dance around a little: collect pointers to all
    // SubscriptionStates, call on all those pointers we haven't yet called on, repeat if there are
    // now more SubscriptionStates.
    absl::flat_hash_map<std::string, DeltaSubscriptionState*> all_subscribed;
    absl::flat_hash_map<std::string, DeltaSubscriptionState*> already_called;
    do {
      for (auto& sub : subscriptions_) {
        all_subscribed[sub.first] = &sub.second->sub_state_;
      }
      for (auto& sub : all_subscribed) {
        if (already_called.insert(sub).second) { // insert succeeded --> not already called
          sub.second->handleEstablishmentFailure();
        }
      }
    } while (all_subscribed.size() != subscriptions_.size());
  }

  void onWriteable() override { trySendDiscoveryRequests(); }

  void kickOffAck(UpdateAck ack) {
    pausable_ack_queue_.push(ack);
    trySendDiscoveryRequests();
  }

  // TODO TODO but yeah this should just be gone!!!!!!!!!
  GrpcMuxWatchPtr subscribe(const std::string&, const std::set<std::string>&,
                            GrpcMuxCallbacks&) override {
    // not sure what GrpcMuxCallbacks is for, but we need a SubscriptionCallbacks here, so......
    //   return std::make_unique<TokenizedGrpcMuxWatch>(*this, addWatch(type_url, resources,
    //   callbacks));
    NOT_IMPLEMENTED_GCOVR_EXCL_LINE;
  }
  void start() override { grpc_stream_.establishNewStream(); }

private:
  WatchPtr addWatch(const std::string& type_url, const std::set<std::string>& resources,
                    SubscriptionCallbacks& callbacks,
                    std::chrono::milliseconds init_fetch_timeout) {
    auto entry = subscriptions_.find(type_url);
    if (entry == subscriptions_.end()) {
      // We don't yet have a subscription for type_url! Make one!
      addSubscription(type_url, init_fetch_timeout);
      return addWatch(type_url, resources, callbacks, init_fetch_timeout);
    }

    WatchPtr watch = entry->second->watch_map_.addWatch(callbacks);
    // updateWatch() queues a discovery request if any of 'resources' are not yet subscribed.
    updateWatch(type_url, watch.get(), resources);
    return watch;
  }

  // Updates the list of resource names watched by the given watch. If an added name is new across
  // the whole subscription, or if a removed name has no other watch interested in it, then the
  // subscription will enqueue and attempt to send an appropriate discovery request.
  void updateWatch(const std::string& type_url, Watch* watch,
                   const std::set<std::string>& resources) {
    ASSERT(watch != nullptr);
    auto sub = subscriptions_.find(type_url);
    if (sub == subscriptions_.end()) {
      ENVOY_LOG(error, "Watch of {} has no subscription to update.", type_url);
      return;
    }
    auto added_removed = sub->second->watch_map_.updateWatchInterest(watch, resources);
    sub->second->sub_state_.updateSubscriptionInterest(added_removed.added_,
                                                       added_removed.removed_);
    // Tell the server about our new interests, if there are any.
    trySendDiscoveryRequests();
  }

  void addSubscription(const std::string& type_url, std::chrono::milliseconds init_fetch_timeout) {
    subscriptions_.emplace(type_url, std::make_unique<SubscriptionStuff>(
                                         type_url, init_fetch_timeout, dispatcher_, local_info_));
    subscription_ordering_.emplace_back(type_url);
  }

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
        ENVOY_LOG(error, "Not sending discovery request for non-existent subscription {}.",
                  next_request_type_url);
        // It's safe to assume the front of the ACK queue is of this type, because that's the only
        // way whoWantsToSendDiscoveryRequest() could return something for a non-existent
        // subscription.
        pausable_ack_queue_.pop();
        continue;
      }
      // Try again later if paused/rate limited/stream down.
      if (!canSendDiscoveryRequest(next_request_type_url)) {
        break;
      }
      // Get our subscription state to generate the appropriate DeltaDiscoveryRequest, and send.
      if (!pausable_ack_queue_.empty()) {
        // Because ACKs take precedence over plain requests, if there is anything in the queue, it's
        // safe to assume it's of the type_url that we're wanting to send.
        grpc_stream_.sendMessage(
            sub->second->sub_state_.getNextRequestWithAck(pausable_ack_queue_.front()));
        pausable_ack_queue_.pop();
      } else {
        grpc_stream_.sendMessage(sub->second->sub_state_.getNextRequestAckless());
      }
    }
    grpc_stream_.maybeUpdateQueueSizeStat(pausable_ack_queue_.size());
  }

  // Checks whether external conditions allow sending a DeltaDiscoveryRequest. (Does not check
  // whether we *want* to send a DeltaDiscoveryRequest).
  bool canSendDiscoveryRequest(const std::string& type_url) {
    if (pausable_ack_queue_.paused(type_url)) {
      ASSERT(false,
             fmt::format("canSendDiscoveryRequest() called on paused type_url {}. Pausedness is "
                         "supposed to be filtered out by whoWantsToSendDiscoveryRequest(). "
                         "Returning false, but your xDS might be about to get head-of-line blocked "
                         "- permanently, if the pause is never undone.",
                         type_url));
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
    // type_url from pausable_ack_queue_ if possible, before looking at pending updates.
    if (!pausable_ack_queue_.empty()) {
      return pausable_ack_queue_.front().type_url_;
    }
    // If we're looking to send multiple non-ACK requests, send them in the order that their
    // subscriptions were initiated.
    for (const auto& sub_type : subscription_ordering_) {
      auto sub = subscriptions_.find(sub_type);
      if (sub == subscriptions_.end()) {
        continue;
      }
      if (sub->second->sub_state_.subscriptionUpdatePending() &&
          !pausable_ack_queue_.paused(sub_type)) {
        return sub->first;
      }
    }
    return absl::nullopt;
  }

  Event::Dispatcher& dispatcher_;
  const LocalInfo::LocalInfo& local_info_;

  // Resource (N)ACKs we're waiting to send, stored in the order that they should be sent in. All
  // of our different resource types' ACKs are mixed together in this queue. See class for
  // description of how it interacts with pause() and resume().
  PausableAckQueue pausable_ack_queue_;

  struct SubscriptionStuff {
    SubscriptionStuff(const std::string& type_url, std::chrono::milliseconds init_fetch_timeout,
                      Event::Dispatcher& dispatcher, const LocalInfo::LocalInfo& local_info)
        : sub_state_(type_url, watch_map_, local_info, init_fetch_timeout, dispatcher),
          init_fetch_timeout_(init_fetch_timeout) {}

    WatchMapImpl watch_map_;
    DeltaSubscriptionState sub_state_;
    const std::chrono::milliseconds init_fetch_timeout_;

  private:
    SubscriptionStuff(const SubscriptionStuff&) = delete;
    SubscriptionStuff& operator=(const SubscriptionStuff&) = delete;
  };
  // Map key is type_url.
  absl::flat_hash_map<std::string, std::unique_ptr<SubscriptionStuff>> subscriptions_;

  // Determines the order of initial discovery requests. (Assumes that subscriptions are added in
  // the order of Envoy's dependency ordering).
  std::list<std::string> subscription_ordering_;

  GrpcStream<envoy::api::v2::DeltaDiscoveryRequest, envoy::api::v2::DeltaDiscoveryResponse>
      grpc_stream_;
};

typedef std::shared_ptr<GrpcDeltaXdsContext> GrpcDeltaXdsContextSharedPtr;

} // namespace Config
} // namespace Envoy
