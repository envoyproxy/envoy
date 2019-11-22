#pragma once

#include "envoy/api/v2/discovery.pb.h"
#include "envoy/common/token_bucket.h"
#include "envoy/config/grpc_mux.h"
#include "envoy/config/subscription.h"

#include "common/common/logger.h"
#include "common/config/delta_subscription_state.h"
#include "common/config/grpc_stream.h"
#include "common/config/pausable_ack_queue.h"
#include "common/config/watch_map.h"
#include "common/grpc/common.h"

namespace Envoy {
namespace Config {

// Manages subscriptions to one or more type of resource. The logical protocol
// state of those subscription(s) is handled by DeltaSubscriptionState.
// This class owns the GrpcStream used to talk to the server, maintains queuing
// logic to properly order the subscription(s)' various messages, and allows
// starting/stopping/pausing of the subscriptions.
class NewGrpcMuxImpl : public GrpcMux,
                       public GrpcStreamCallbacks<envoy::api::v2::DeltaDiscoveryResponse>,
                       Logger::Loggable<Logger::Id::config> {
public:
  NewGrpcMuxImpl(Grpc::RawAsyncClientPtr&& async_client, Event::Dispatcher& dispatcher,
                 const Protobuf::MethodDescriptor& service_method, Runtime::RandomGenerator& random,
                 Stats::Scope& scope, const RateLimitSettings& rate_limit_settings,
                 const LocalInfo::LocalInfo& local_info);

  Watch* addOrUpdateWatch(const std::string& type_url, Watch* watch,
                          const std::set<std::string>& resources, SubscriptionCallbacks& callbacks,
                          std::chrono::milliseconds init_fetch_timeout) override;
  void removeWatch(const std::string& type_url, Watch* watch) override;

  // TODO(fredlas) PR #8478 will remove this.
  bool isDelta() const override { return true; }

  void pause(const std::string& type_url) override;
  void resume(const std::string& type_url) override;
  bool paused(const std::string& type_url) const override;
  void
  onDiscoveryResponse(std::unique_ptr<envoy::api::v2::DeltaDiscoveryResponse>&& message) override;

  void onStreamEstablished() override;

  void onEstablishmentFailure() override;

  void onWriteable() override;

  void kickOffAck(UpdateAck ack);

  // TODO(fredlas) remove these two from the GrpcMux interface.
  GrpcMuxWatchPtr subscribe(const std::string&, const std::set<std::string>&,
                            GrpcMuxCallbacks&) override;
  void start() override;

private:
  Watch* addWatch(const std::string& type_url, const std::set<std::string>& resources,
                  SubscriptionCallbacks& callbacks, std::chrono::milliseconds init_fetch_timeout);

  // Updates the list of resource names watched by the given watch. If an added name is new across
  // the whole subscription, or if a removed name has no other watch interested in it, then the
  // subscription will enqueue and attempt to send an appropriate discovery request.
  void updateWatch(const std::string& type_url, Watch* watch,
                   const std::set<std::string>& resources);

  void addSubscription(const std::string& type_url, std::chrono::milliseconds init_fetch_timeout);

  void trySendDiscoveryRequests();

  // Checks whether external conditions allow sending a DeltaDiscoveryRequest. (Does not check
  // whether we *want* to send a DeltaDiscoveryRequest).
  bool canSendDiscoveryRequest(const std::string& type_url);

  // Checks whether we have something to say in a DeltaDiscoveryRequest, which can be an ACK and/or
  // a subscription update. (Does not check whether we *can* send that DeltaDiscoveryRequest).
  // Returns the type_url we should send the DeltaDiscoveryRequest for (if any).
  // First, prioritizes ACKs over non-ACK subscription interest updates.
  // Then, prioritizes non-ACK updates in the order the various types
  // of subscriptions were activated.
  absl::optional<std::string> whoWantsToSendDiscoveryRequest();

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

    WatchMap watch_map_;
    DeltaSubscriptionState sub_state_;
    const std::chrono::milliseconds init_fetch_timeout_;

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

using NewGrpcMuxImplSharedPtr = std::shared_ptr<NewGrpcMuxImpl>;

} // namespace Config
} // namespace Envoy
