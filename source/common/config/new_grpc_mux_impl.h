#pragma once

#include <memory>

#include "envoy/api/v2/discovery.pb.h"
#include "envoy/common/random_generator.h"
#include "envoy/common/token_bucket.h"
#include "envoy/config/grpc_mux.h"
#include "envoy/config/subscription.h"
#include "envoy/service/discovery/v3/discovery.pb.h"

#include "common/common/logger.h"
#include "common/config/api_version.h"
#include "common/config/delta_subscription_state.h"
#include "common/config/grpc_stream.h"
#include "common/config/pausable_ack_queue.h"
#include "common/config/watch_map.h"
#include "common/grpc/common.h"
#include "common/runtime/runtime_features.h"

namespace Envoy {
namespace Config {

// Manages subscriptions to one or more type of resource. The logical protocol
// state of those subscription(s) is handled by DeltaSubscriptionState.
// This class owns the GrpcStream used to talk to the server, maintains queuing
// logic to properly order the subscription(s)' various messages, and allows
// starting/stopping/pausing of the subscriptions.
class NewGrpcMuxImpl
    : public GrpcMux,
      public GrpcStreamCallbacks<envoy::service::discovery::v3::DeltaDiscoveryResponse>,
      Logger::Loggable<Logger::Id::config> {
public:
  NewGrpcMuxImpl(Grpc::RawAsyncClientPtr&& async_client, Event::Dispatcher& dispatcher,
                 const Protobuf::MethodDescriptor& service_method,
                 envoy::config::core::v3::ApiVersion transport_api_version,
                 Random::RandomGenerator& random, Stats::Scope& scope,
                 const RateLimitSettings& rate_limit_settings,
                 const LocalInfo::LocalInfo& local_info);

  GrpcMuxWatchPtr addWatch(const std::string& type_url, const std::set<std::string>& resources,
                           SubscriptionCallbacks& callbacks,
                           OpaqueResourceDecoder& resource_decoder,
                           const bool use_namespace_matching = false) override;

  void requestOnDemandUpdate(const std::string& type_url,
                             const std::set<std::string>& for_update) override;

  ScopedResume pause(const std::string& type_url) override;
  ScopedResume pause(const std::vector<std::string> type_urls) override;

  void registerVersionedTypeUrl(const std::string& type_url);

  void onDiscoveryResponse(
      std::unique_ptr<envoy::service::discovery::v3::DeltaDiscoveryResponse>&& message,
      ControlPlaneStats& control_plane_stats) override;

  void onStreamEstablished() override;

  void onEstablishmentFailure() override;

  void onWriteable() override;

  void kickOffAck(UpdateAck ack);

  // TODO(fredlas) remove this from the GrpcMux interface.
  void start() override;

  struct SubscriptionStuff {
    SubscriptionStuff(const std::string& type_url, const LocalInfo::LocalInfo& local_info,
                      const bool use_namespace_matching)
        : watch_map_(use_namespace_matching), sub_state_(type_url, watch_map_, local_info) {}

    WatchMap watch_map_;
    DeltaSubscriptionState sub_state_;

    SubscriptionStuff(const SubscriptionStuff&) = delete;
    SubscriptionStuff& operator=(const SubscriptionStuff&) = delete;
  };

  using SubscriptionStuffPtr = std::unique_ptr<SubscriptionStuff>;

  // for use in tests only
  const absl::flat_hash_map<std::string, SubscriptionStuffPtr>& subscriptions() {
    return subscriptions_;
  }

private:
  class WatchImpl : public GrpcMuxWatch {
  public:
    WatchImpl(const std::string& type_url, Watch* watch, NewGrpcMuxImpl& parent)
        : type_url_(type_url), watch_(watch), parent_(parent) {}

    ~WatchImpl() override { remove(); }

    void remove() {
      if (watch_) {
        parent_.removeWatch(type_url_, watch_);
        watch_ = nullptr;
      }
    }

    void update(const std::set<std::string>& resources) override {
      parent_.updateWatch(type_url_, watch_, resources);
    }

  private:
    const std::string type_url_;
    Watch* watch_;
    NewGrpcMuxImpl& parent_;
  };

  void removeWatch(const std::string& type_url, Watch* watch);

  // Updates the list of resource names watched by the given watch. If an added name is new across
  // the whole subscription, or if a removed name has no other watch interested in it, then the
  // subscription will enqueue and attempt to send an appropriate discovery request.
  void updateWatch(const std::string& type_url, Watch* watch,
                   const std::set<std::string>& resources,
                   const bool creating_namespace_watch = false);

  void addSubscription(const std::string& type_url, const bool use_namespace_matching);

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

  // Resource (N)ACKs we're waiting to send, stored in the order that they should be sent in. All
  // of our different resource types' ACKs are mixed together in this queue. See class for
  // description of how it interacts with pause() and resume().
  PausableAckQueue pausable_ack_queue_;

  // Map key is type_url.
  absl::flat_hash_map<std::string, SubscriptionStuffPtr> subscriptions_;

  // Determines the order of initial discovery requests. (Assumes that subscriptions are added in
  // the order of Envoy's dependency ordering).
  std::list<std::string> subscription_ordering_;

  GrpcStream<envoy::service::discovery::v3::DeltaDiscoveryRequest,
             envoy::service::discovery::v3::DeltaDiscoveryResponse>
      grpc_stream_;

  const LocalInfo::LocalInfo& local_info_;

  const envoy::config::core::v3::ApiVersion transport_api_version_;

  const bool enable_type_url_downgrade_and_upgrade_;
};

using NewGrpcMuxImplPtr = std::unique_ptr<NewGrpcMuxImpl>;
using NewGrpcMuxImplSharedPtr = std::shared_ptr<NewGrpcMuxImpl>;

} // namespace Config
} // namespace Envoy
