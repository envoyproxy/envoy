#pragma once

#include <memory>

#include "envoy/common/random_generator.h"
#include "envoy/common/token_bucket.h"
#include "envoy/config/endpoint/v3/endpoint.pb.h"
#include "envoy/config/grpc_mux.h"
#include "envoy/config/subscription.h"
#include "envoy/config/xds_config_tracker.h"
#include "envoy/service/discovery/v3/discovery.pb.h"

#include "source/common/common/logger.h"
#include "source/common/config/api_version.h"
#include "source/common/config/resource_name.h"
#include "source/common/grpc/common.h"
#include "source/common/runtime/runtime_features.h"
#include "source/extensions/config_subscription/grpc/delta_subscription_state.h"
#include "source/extensions/config_subscription/grpc/grpc_mux_context.h"
#include "source/extensions/config_subscription/grpc/grpc_mux_failover.h"
#include "source/extensions/config_subscription/grpc/pausable_ack_queue.h"
#include "source/extensions/config_subscription/grpc/watch_map.h"

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
  NewGrpcMuxImpl(GrpcMuxContext& grpc_mux_context);

  ~NewGrpcMuxImpl() override;

  // Causes all NewGrpcMuxImpl objects to stop sending any messages on `grpc_stream_` to fix a crash
  // on Envoy shutdown due to dangling pointers. This may not be the ideal fix; it is probably
  // preferable for the `ServerImpl` to cause all configuration subscriptions to be shutdown, which
  // would then cause all `NewGrpcMuxImpl` to be destructed.
  // TODO: figure out the correct fix: https://github.com/envoyproxy/envoy/issues/15072.
  static void shutdownAll();

  void shutdown() { shutdown_ = true; }

  GrpcMuxWatchPtr addWatch(const std::string& type_url,
                           const absl::flat_hash_set<std::string>& resources,
                           SubscriptionCallbacks& callbacks,
                           OpaqueResourceDecoderSharedPtr resource_decoder,
                           const SubscriptionOptions& options) override;

  void requestOnDemandUpdate(const std::string& type_url,
                             const absl::flat_hash_set<std::string>& for_update) override;

  EdsResourcesCacheOptRef edsResourcesCache() override {
    return makeOptRefFromPtr(eds_resources_cache_.get());
  }

  ScopedResume pause(const std::string& type_url) override;
  ScopedResume pause(const std::vector<std::string> type_urls) override;

  void onDiscoveryResponse(
      std::unique_ptr<envoy::service::discovery::v3::DeltaDiscoveryResponse>&& message,
      ControlPlaneStats& control_plane_stats) override;

  void onStreamEstablished() override;

  void onEstablishmentFailure(bool next_attempt_may_send_initial_resource_version) override;

  void onWriteable() override;

  void kickOffAck(UpdateAck ack);

  // TODO(fredlas) remove this from the GrpcMux interface.
  void start() override;

  absl::Status
  updateMuxSource(Grpc::RawAsyncClientPtr&& primary_async_client,
                  Grpc::RawAsyncClientPtr&& failover_async_client,
                  CustomConfigValidatorsPtr&& custom_config_validators, Stats::Scope& scope,
                  BackOffStrategyPtr&& backoff_strategy,
                  const envoy::config::core::v3::ApiConfigSource& ads_config_source) override;

  GrpcStreamInterface<envoy::service::discovery::v3::DeltaDiscoveryRequest,
                      envoy::service::discovery::v3::DeltaDiscoveryResponse>&
  grpcStreamForTest() {
    // TODO(adisuissa): Once envoy.restart_features.xds_failover_support is deprecated,
    // return grpc_stream_.currentStreamForTest() directly (defined in GrpcMuxFailover).
    if (Runtime::runtimeFeatureEnabled("envoy.restart_features.xds_failover_support")) {
      return dynamic_cast<GrpcMuxFailover<envoy::service::discovery::v3::DeltaDiscoveryRequest,
                                          envoy::service::discovery::v3::DeltaDiscoveryResponse>*>(
                 grpc_stream_.get())
          ->currentStreamForTest();
    }
    return *grpc_stream_.get();
  }

  struct SubscriptionStuff {
    SubscriptionStuff(const std::string& type_url, const LocalInfo::LocalInfo& local_info,
                      const bool use_namespace_matching, Event::Dispatcher& dispatcher,
                      CustomConfigValidators* config_validators,
                      XdsConfigTrackerOptRef xds_config_tracker,
                      EdsResourcesCacheOptRef eds_resources_cache)
        : watch_map_(use_namespace_matching, type_url, config_validators, eds_resources_cache),
          sub_state_(type_url, watch_map_, local_info, dispatcher, xds_config_tracker) {
      // If eds resources cache is provided, then the type must be ClusterLoadAssignment.
      ASSERT(
          !eds_resources_cache.has_value() ||
          (type_url == Config::getTypeUrl<envoy::config::endpoint::v3::ClusterLoadAssignment>()));
    }

    WatchMap watch_map_;
    DeltaSubscriptionState sub_state_;
    std::string control_plane_identifier_{};

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
    WatchImpl(const std::string& type_url, Watch* watch, NewGrpcMuxImpl& parent,
              const SubscriptionOptions& options)
        : type_url_(type_url), watch_(watch), parent_(parent), options_(options) {}

    ~WatchImpl() override { remove(); }

    void remove() {
      if (watch_) {
        parent_.removeWatch(type_url_, watch_);
        watch_ = nullptr;
      }
    }

    void update(const absl::flat_hash_set<std::string>& resources) override {
      parent_.updateWatch(type_url_, watch_, resources, options_);
    }

  private:
    const std::string type_url_;
    Watch* watch_;
    NewGrpcMuxImpl& parent_;
    const SubscriptionOptions options_;
  };

  // Helper function to create the grpc_stream_ object.
  std::unique_ptr<GrpcStreamInterface<envoy::service::discovery::v3::DeltaDiscoveryRequest,
                                      envoy::service::discovery::v3::DeltaDiscoveryResponse>>
  createGrpcStreamObject(Grpc::RawAsyncClientPtr&& async_client,
                         Grpc::RawAsyncClientPtr&& failover_async_client,
                         const Protobuf::MethodDescriptor& service_method, Stats::Scope& scope,
                         BackOffStrategyPtr&& backoff_strategy,
                         const RateLimitSettings& rate_limit_settings);

  void removeWatch(const std::string& type_url, Watch* watch);

  // Updates the list of resource names watched by the given watch. If an added name is new across
  // the whole subscription, or if a removed name has no other watch interested in it, then the
  // subscription will enqueue and attempt to send an appropriate discovery request.
  void updateWatch(const std::string& type_url, Watch* watch,
                   const absl::flat_hash_set<std::string>& resources,
                   const SubscriptionOptions& options);

  // Adds a subscription for the type_url to the subscriptions map and order list.
  void addSubscription(const std::string& type_url, bool use_namespace_matching);

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

  // Invoked when dynamic context parameters change for a resource type.
  void onDynamicContextUpdate(absl::string_view resource_type_url);

  // Resource (N)ACKs we're waiting to send, stored in the order that they should be sent in. All
  // of our different resource types' ACKs are mixed together in this queue. See class for
  // description of how it interacts with pause() and resume().
  PausableAckQueue pausable_ack_queue_;

  // Map key is type_url.
  absl::flat_hash_map<std::string, SubscriptionStuffPtr> subscriptions_;

  // Determines the order of initial discovery requests. (Assumes that subscriptions are added in
  // the order of Envoy's dependency ordering).
  std::list<std::string> subscription_ordering_;

  Event::Dispatcher& dispatcher_;
  // Multiplexes the stream to the primary and failover sources.
  // TODO(adisuissa): Once envoy.restart_features.xds_failover_support is deprecated,
  // convert from unique_ptr<GrpcStreamInterface> to GrpcMuxFailover directly.
  std::unique_ptr<GrpcStreamInterface<envoy::service::discovery::v3::DeltaDiscoveryRequest,
                                      envoy::service::discovery::v3::DeltaDiscoveryResponse>>
      grpc_stream_;

  const LocalInfo::LocalInfo& local_info_;
  CustomConfigValidatorsPtr config_validators_;
  Common::CallbackHandlePtr dynamic_update_callback_handle_;
  XdsConfigTrackerOptRef xds_config_tracker_;
  EdsResourcesCachePtr eds_resources_cache_;

  // Used to track whether initial_resource_versions should be populated on the
  // next reconnection.
  bool should_send_initial_resource_versions_{true};
  bool started_{false};
  // True iff Envoy is shutting down; no messages should be sent on the `grpc_stream_` when this is
  // true because it may contain dangling pointers.
  std::atomic<bool> shutdown_{false};
};

using NewGrpcMuxImplPtr = std::unique_ptr<NewGrpcMuxImpl>;
using NewGrpcMuxImplSharedPtr = std::shared_ptr<NewGrpcMuxImpl>;

class NewGrpcMuxFactory;
DECLARE_FACTORY(NewGrpcMuxFactory);

} // namespace Config
} // namespace Envoy
