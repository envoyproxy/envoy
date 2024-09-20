#pragma once

#include <cstdint>
#include <memory>
#include <queue>

#include "envoy/common/random_generator.h"
#include "envoy/common/time.h"
#include "envoy/common/token_bucket.h"
#include "envoy/config/custom_config_validators.h"
#include "envoy/config/grpc_mux.h"
#include "envoy/config/subscription.h"
#include "envoy/config/xds_config_tracker.h"
#include "envoy/config/xds_resources_delegate.h"
#include "envoy/event/dispatcher.h"
#include "envoy/grpc/status.h"
#include "envoy/service/discovery/v3/discovery.pb.h"
#include "envoy/upstream/cluster_manager.h"

#include "source/common/common/logger.h"
#include "source/common/common/utility.h"
#include "source/common/config/api_version.h"
#include "source/common/grpc/common.h"
#include "source/extensions/config_subscription/grpc/grpc_mux_context.h"
#include "source/extensions/config_subscription/grpc/grpc_mux_failover.h"
#include "source/extensions/config_subscription/grpc/pausable_ack_queue.h"
#include "source/extensions/config_subscription/grpc/watch_map.h"
#include "source/extensions/config_subscription/grpc/xds_mux/delta_subscription_state.h"
#include "source/extensions/config_subscription/grpc/xds_mux/sotw_subscription_state.h"

#include "absl/container/node_hash_map.h"

namespace Envoy {
namespace Config {
namespace XdsMux {

class ShutdownableMux {
public:
  virtual ~ShutdownableMux() = default;
  virtual void shutdown() PURE;
};

// Manages subscriptions to one or more type of resource. The logical protocol
// state of those subscription(s) is handled by SubscriptionState.
// This class owns the GrpcStream used to talk to the server, maintains queuing
// logic to properly order the subscription(s)' various messages, and allows
// starting/stopping/pausing of the subscriptions.
//
// @tparam S SubscriptionState state type, either SotwSubscriptionState or DeltaSubscriptionState
// @tparam F SubscriptionStateFactory type, either SotwSubscriptionStateFactory or
// DeltaSubscriptionStateFactory
// @tparam RQ Xds request type, either envoy::service::discovery::v3::DiscoveryRequest or
// envoy::service::discovery::v3::DeltaDiscoveryRequest
// @tparam RS Xds response type, either envoy::service::discovery::v3::DiscoveryResponse or
// envoy::service::discovery::v3::DeltaDiscoveryResponse
//
template <class S, class F, class RQ, class RS>
class GrpcMuxImpl : public GrpcStreamCallbacks<RS>,
                    public GrpcMux,
                    public ShutdownableMux,
                    Logger::Loggable<Logger::Id::config> {
public:
  GrpcMuxImpl(std::unique_ptr<F> subscription_state_factory, GrpcMuxContext& grpc_mux_context,
              bool skip_subsequent_node);

  ~GrpcMuxImpl() override;

  // Causes all GrpcMuxImpl objects to stop sending any messages on `grpc_stream_` to fix a crash
  // on Envoy shutdown due to dangling pointers. This may not be the ideal fix; it is probably
  // preferable for the `ServerImpl` to cause all configuration subscriptions to be shutdown, which
  // would then cause all `GrpcMuxImpl` to be destructed.
  // TODO: figure out the correct fix: https://github.com/envoyproxy/envoy/issues/15072.
  static void shutdownAll();

  void shutdown() override { shutdown_ = true; }
  bool isShutdown() { return shutdown_; }

  // TODO (dmitri-d) return a naked pointer instead of the wrapper once the legacy mux has been
  // removed and the mux interface can be changed
  Config::GrpcMuxWatchPtr addWatch(const std::string& type_url,
                                   const absl::flat_hash_set<std::string>& resources,
                                   SubscriptionCallbacks& callbacks,
                                   OpaqueResourceDecoderSharedPtr resource_decoder,
                                   const SubscriptionOptions& options) override;
  void updateWatch(const std::string& type_url, Watch* watch,
                   const absl::flat_hash_set<std::string>& resources,
                   const SubscriptionOptions& options);
  void removeWatch(const std::string& type_url, Watch* watch);

  ScopedResume pause(const std::string& type_url) override;
  ScopedResume pause(const std::vector<std::string> type_urls) override;
  void start() override;
  const absl::flat_hash_map<std::string, std::unique_ptr<S>>& subscriptions() const {
    return subscriptions_;
  }

  // GrpcStreamCallbacks
  void onStreamEstablished() override { handleEstablishedStream(); }
  void onEstablishmentFailure(bool next_attempt_may_send_initial_resource_version) override {
    handleStreamEstablishmentFailure(next_attempt_may_send_initial_resource_version);
  }
  void onWriteable() override { trySendDiscoveryRequests(); }
  void onDiscoveryResponse(std::unique_ptr<RS>&& message,
                           ControlPlaneStats& control_plane_stats) override {
    genericHandleResponse(message->type_url(), *message, control_plane_stats);
  }

  absl::Status
  updateMuxSource(Grpc::RawAsyncClientPtr&& primary_async_client,
                  Grpc::RawAsyncClientPtr&& failover_async_client,
                  CustomConfigValidatorsPtr&& custom_config_validators, Stats::Scope& scope,
                  BackOffStrategyPtr&& backoff_strategy,
                  const envoy::config::core::v3::ApiConfigSource& ads_config_source) override;

  EdsResourcesCacheOptRef edsResourcesCache() override {
    return makeOptRefFromPtr(eds_resources_cache_.get());
  }

  GrpcStreamInterface<RQ, RS>& grpcStreamForTest() {
    // TODO(adisuissa): Once envoy.restart_features.xds_failover_support is deprecated,
    // return grpc_stream_.currentStreamForTest() directly (defined in GrpcMuxFailover).
    if (Runtime::runtimeFeatureEnabled("envoy.restart_features.xds_failover_support")) {
      return dynamic_cast<GrpcMuxFailover<RQ, RS>*>(grpc_stream_.get())->currentStreamForTest();
    }
    return *grpc_stream_.get();
  }

protected:
  class WatchImpl : public Envoy::Config::GrpcMuxWatch {
  public:
    WatchImpl(const std::string& type_url, Watch* watch, GrpcMuxImpl& parent,
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
    GrpcMuxImpl& parent_;
    const SubscriptionOptions options_;
  };

  void sendGrpcMessage(RQ& msg_proto, S& sub_state);
  void maybeUpdateQueueSizeStat(uint64_t size) { grpc_stream_->maybeUpdateQueueSizeStat(size); }
  bool grpcStreamAvailable() { return grpc_stream_->grpcStreamAvailable(); }
  bool rateLimitAllowsDrain() { return grpc_stream_->checkRateLimitAllowsDrain(); }
  void sendMessage(RQ& msg_proto) { grpc_stream_->sendMessage(msg_proto); }

  S& subscriptionStateFor(const std::string& type_url);
  WatchMap& watchMapFor(const std::string& type_url);
  void handleEstablishedStream();
  void handleStreamEstablishmentFailure(bool next_attempt_may_send_initial_resource_version);
  void genericHandleResponse(const std::string& type_url, const RS& response_proto,
                             ControlPlaneStats& control_plane_stats);
  void trySendDiscoveryRequests();
  bool skipSubsequentNode() const { return skip_subsequent_node_; }
  bool anyRequestSentYetInCurrentStream() const { return any_request_sent_yet_in_current_stream_; }
  void setAnyRequestSentYetInCurrentStream(bool value) {
    any_request_sent_yet_in_current_stream_ = value;
  }
  const LocalInfo::LocalInfo& localInfo() const { return local_info_; }

  virtual absl::string_view methodName() const PURE;

private:
  // Helper function to create the grpc_stream_ object.
  // TODO(adisuissa): this should be removed when envoy.restart_features.xds_failover_support
  // is deprecated.
  std::unique_ptr<GrpcStreamInterface<RQ, RS>> createGrpcStreamObject(
      Grpc::RawAsyncClientPtr&& async_client, Grpc::RawAsyncClientPtr&& failover_async_client,
      const Protobuf::MethodDescriptor& service_method, Stats::Scope& scope,
      BackOffStrategyPtr&& backoff_strategy, const RateLimitSettings& rate_limit_settings);

  // Checks whether external conditions allow sending a DeltaDiscoveryRequest. (Does not check
  // whether we *want* to send a (Delta)DiscoveryRequest).
  bool canSendDiscoveryRequest(const std::string& type_url);

  // Checks whether we have something to say in a (Delta)DiscoveryRequest, which can be an ACK
  // and/or a subscription update. (Does not check whether we *can* send that
  // (Delta)DiscoveryRequest). Returns the type_url we should send the DeltaDiscoveryRequest for (if
  // any). First, prioritizes ACKs over non-ACK subscription interest updates. Then, prioritizes
  // non-ACK updates in the order the various types of subscriptions were activated (as tracked by
  // subscription_ordering_).
  absl::optional<std::string> whoWantsToSendDiscoveryRequest();

  // Invoked when dynamic context parameters change for a resource type.
  void onDynamicContextUpdate(absl::string_view resource_type_url);

  Event::Dispatcher& dispatcher_;
  // Multiplexes the stream to the primary and failover sources.
  // TODO(adisuissa): Once envoy.restart_features.xds_failover_support is deprecated,
  // convert from unique_ptr<GrpcStreamInterface> to GrpcMuxFailover directly.
  std::unique_ptr<GrpcStreamInterface<RQ, RS>> grpc_stream_;

  // Resource (N)ACKs we're waiting to send, stored in the order that they should be sent in. All
  // of our different resource types' ACKs are mixed together in this queue. See class for
  // description of how it interacts with pause() and resume().
  PausableAckQueue pausable_ack_queue_;

  // Makes SubscriptionStates, to be held in the subscriptions_ map. Whether this GrpcMux is doing
  // delta or state of the world xDS is determined by which concrete subclass this variable gets.
  std::unique_ptr<F> subscription_state_factory_;

  // Map key is type_url.
  // Only addWatch() should insert into these maps.
  absl::flat_hash_map<std::string, std::unique_ptr<S>> subscriptions_;
  absl::flat_hash_map<std::string, std::unique_ptr<WatchMap>> watch_maps_;

  // Determines the order of initial discovery requests. (Assumes that subscriptions are added
  // to this GrpcMux in the order of Envoy's dependency ordering).
  std::list<std::string> subscription_ordering_;

  // Whether to enable the optimization of only including the node field in the very first
  // discovery request in an xDS gRPC stream (really just one: *not* per-type_url).
  const bool skip_subsequent_node_;

  // State to help with skip_subsequent_node's logic.
  bool any_request_sent_yet_in_current_stream_{};

  // Used to populate the (Delta)DiscoveryRequest's node field. That field is the same across
  // all type_urls, and moreover, the 'skip_subsequent_node' logic needs to operate across all
  // the type_urls. So, while the SubscriptionStates populate every other field of these messages,
  // this one is up to GrpcMux.
  const LocalInfo::LocalInfo& local_info_;
  Common::CallbackHandlePtr dynamic_update_callback_handle_;
  CustomConfigValidatorsPtr config_validators_;
  XdsConfigTrackerOptRef xds_config_tracker_;
  XdsResourcesDelegateOptRef xds_resources_delegate_;
  EdsResourcesCachePtr eds_resources_cache_;
  const std::string target_xds_authority_;

  // Used to track whether initial_resource_versions should be populated on the
  // next reconnection.
  bool should_send_initial_resource_versions_{true};

  bool started_{false};
  // True iff Envoy is shutting down; no messages should be sent on the `grpc_stream_` when this is
  // true because it may contain dangling pointers.
  std::atomic<bool> shutdown_{false};
};

class GrpcMuxDelta : public GrpcMuxImpl<DeltaSubscriptionState, DeltaSubscriptionStateFactory,
                                        envoy::service::discovery::v3::DeltaDiscoveryRequest,
                                        envoy::service::discovery::v3::DeltaDiscoveryResponse> {
public:
  GrpcMuxDelta(GrpcMuxContext& grpc_mux_context, bool skip_subsequent_node);

  // GrpcStreamCallbacks
  void requestOnDemandUpdate(const std::string& type_url,
                             const absl::flat_hash_set<std::string>& for_update) override;

private:
  absl::string_view methodName() const override {
    return "envoy.service.discovery.v3.AggregatedDiscoveryService.DeltaAggregatedResources";
  }
};

class GrpcMuxSotw : public GrpcMuxImpl<SotwSubscriptionState, SotwSubscriptionStateFactory,
                                       envoy::service::discovery::v3::DiscoveryRequest,
                                       envoy::service::discovery::v3::DiscoveryResponse> {
public:
  GrpcMuxSotw(GrpcMuxContext& grpc_mux_context, bool skip_subsequent_node);

  // GrpcStreamCallbacks
  void requestOnDemandUpdate(const std::string&, const absl::flat_hash_set<std::string>&) override {
    ENVOY_BUG(false, "unexpected request for on demand update");
  }

private:
  absl::string_view methodName() const override {
    return "envoy.service.discovery.v3.AggregatedDiscoveryService.StreamAggregatedResources";
  }
};

class NullGrpcMuxImpl : public GrpcMux {
public:
  void start() override {}

  ScopedResume pause(const std::string&) override {
    return std::make_unique<Cleanup>([]() {});
  }
  ScopedResume pause(const std::vector<std::string>) override {
    return std::make_unique<Cleanup>([]() {});
  }

  Config::GrpcMuxWatchPtr addWatch(const std::string&, const absl::flat_hash_set<std::string>&,
                                   SubscriptionCallbacks&, OpaqueResourceDecoderSharedPtr,
                                   const SubscriptionOptions&) override;

  absl::Status updateMuxSource(Grpc::RawAsyncClientPtr&&, Grpc::RawAsyncClientPtr&&,
                               CustomConfigValidatorsPtr&&, Stats::Scope&, BackOffStrategyPtr&&,
                               const envoy::config::core::v3::ApiConfigSource&) override {
    return absl::UnimplementedError("");
  }

  void requestOnDemandUpdate(const std::string&, const absl::flat_hash_set<std::string>&) override {
    ENVOY_BUG(false, "unexpected request for on demand update");
  }

  EdsResourcesCacheOptRef edsResourcesCache() override { return {}; }
};

} // namespace XdsMux
} // namespace Config
} // namespace Envoy
