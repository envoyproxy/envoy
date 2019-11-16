#pragma once

#include "envoy/api/v2/discovery.pb.h"
#include "envoy/common/token_bucket.h"
#include "envoy/config/grpc_mux.h"
#include "envoy/config/subscription.h"

#include "common/common/logger.h"
#include "common/config/delta_subscription_state.h"
#include "common/config/grpc_stream.h"
#include "common/config/pausable_ack_queue.h"
#include "common/config/sotw_subscription_state.h"
#include "common/config/watch_map.h"
#include "common/grpc/common.h"

namespace Envoy {
namespace Config {

// Manages subscriptions to one or more type of resource. The logical protocol
// state of those subscription(s) is handled by SubscriptionState.
// This class owns the GrpcStream used to talk to the server, maintains queuing
// logic to properly order the subscription(s)' various messages, and allows
// starting/stopping/pausing of the subscriptions.
class GrpcMuxImpl : public GrpcMux, Logger::Loggable<Logger::Id::config> {
public:
  GrpcMuxImpl(std::unique_ptr<SubscriptionStateFactory> subscription_state_factory,
              bool skip_subsequent_node, const LocalInfo::LocalInfo& local_info);

  Watch* addOrUpdateWatch(const std::string& type_url, Watch* watch,
                          const std::set<std::string>& resources, SubscriptionCallbacks& callbacks,
                          std::chrono::milliseconds init_fetch_timeout) override;
  void removeWatch(const std::string& type_url, Watch* watch) override;

  void pause(const std::string& type_url) override;
  void resume(const std::string& type_url) override;
  bool paused(const std::string& type_url) const override;
  void start() override;
  void disableInitFetchTimeoutTimer() override;

protected:
  // Everything related to GrpcStream must remain abstract. GrpcStream (and the gRPC-using classes
  // that underlie it) are templated on protobufs. That means that a single implementation that
  // supports different types of protobufs cannot use polymorphism to share code. The workaround:
  // the GrpcStream will be owned by a derived class, and all code that would touch grpc_stream_ is
  // seen here in the base class as calls to abstract functions, to be provided by those derived
  // classes.
  virtual void establishGrpcStream() PURE;
  // Deletes msg_proto_ptr.
  virtual void sendGrpcMessage(void* msg_proto_ptr) PURE;
  virtual void maybeUpdateQueueSizeStat(uint64_t size) PURE;
  virtual bool grpcStreamAvailable() const PURE;
  virtual bool rateLimitAllowsDrain() PURE;

  SubscriptionState& subscriptionStateFor(const std::string& type_url);
  WatchMap& watchMapFor(const std::string& type_url);
  void handleEstablishedStream();
  void handleStreamEstablishmentFailure();
  void genericHandleResponse(const std::string& type_url, const void* response_proto_ptr);
  void trySendDiscoveryRequests();
  bool skip_subsequent_node() const { return skip_subsequent_node_; }
  bool any_request_sent_yet_in_current_stream() const {
    return any_request_sent_yet_in_current_stream_;
  }
  void set_any_request_sent_yet_in_current_stream(bool value) {
    any_request_sent_yet_in_current_stream_ = value;
  }
  const LocalInfo::LocalInfo& local_info() const { return local_info_; }

private:
  Watch* addWatch(const std::string& type_url, const std::set<std::string>& resources,
                  SubscriptionCallbacks& callbacks, std::chrono::milliseconds init_fetch_timeout);

  // Updates the list of resource names watched by the given watch. If an added name is new across
  // the whole subscription, or if a removed name has no other watch interested in it, then the
  // subscription will enqueue and attempt to send an appropriate discovery request.
  void updateWatch(const std::string& type_url, Watch* watch,
                   const std::set<std::string>& resources);

  // Checks whether external conditions allow sending a DeltaDiscoveryRequest. (Does not check
  // whether we *want* to send a DeltaDiscoveryRequest).
  bool canSendDiscoveryRequest(const std::string& type_url);

  // Checks whether we have something to say in a DeltaDiscoveryRequest, which can be an ACK and/or
  // a subscription update. (Does not check whether we *can* send that DeltaDiscoveryRequest).
  // Returns the type_url we should send the DeltaDiscoveryRequest for (if any).
  // First, prioritizes ACKs over non-ACK subscription interest updates.
  // Then, prioritizes non-ACK updates in the order the various types
  // of subscriptions were activated (as tracked by subscription_ordering_).
  absl::optional<std::string> whoWantsToSendDiscoveryRequest();

  // Resource (N)ACKs we're waiting to send, stored in the order that they should be sent in. All
  // of our different resource types' ACKs are mixed together in this queue. See class for
  // description of how it interacts with pause() and resume().
  PausableAckQueue pausable_ack_queue_;

  // Makes SubscriptionStates, to be held in the subscriptions_ map. Whether this GrpcMux is doing
  // delta or state of the world xDS is determined by which concrete subclass this variable gets.
  std::unique_ptr<SubscriptionStateFactory> subscription_state_factory_;

  // Map key is type_url.
  // Only addWatch() should insert into these maps.
  absl::flat_hash_map<std::string, std::unique_ptr<SubscriptionState>> subscriptions_;
  absl::flat_hash_map<std::string, std::unique_ptr<WatchMap>> watch_maps_;

  // Determines the order of initial discovery requests. (Assumes that subscriptions are added
  // to this GrpcMux in the order of Envoy's dependency ordering).
  std::list<std::string> subscription_ordering_;

  // Whether to enable the optimization of only including the node field in the very first
  // discovery request in an xDS gRPC stream (really just one: *not* per-type_url).
  const bool skip_subsequent_node_;

  // State to help with skip_subsequent_node's logic.
  bool any_request_sent_yet_in_current_stream_{};

  // Used to populate the [Delta]DiscoveryRequest's node field. That field is the same across
  // all type_urls, and moreover, the 'skip_subsequent_node' logic needs to operate across all
  // the type_urls. So, while the SubscriptionStates populate every other field of these messages,
  // this one is up to GrpcMux.
  const LocalInfo::LocalInfo& local_info_;
};

class GrpcMuxDelta : public GrpcMuxImpl,
                     public GrpcStreamCallbacks<envoy::api::v2::DeltaDiscoveryResponse> {
public:
  GrpcMuxDelta(Grpc::RawAsyncClientPtr&& async_client, Event::Dispatcher& dispatcher,
               const Protobuf::MethodDescriptor& service_method, Runtime::RandomGenerator& random,
               Stats::Scope& scope, const RateLimitSettings& rate_limit_settings,
               const LocalInfo::LocalInfo& local_info, bool skip_subsequent_node);

  // GrpcStreamCallbacks
  void onStreamEstablished() override;
  void onEstablishmentFailure() override;
  void onWriteable() override;
  void
  onDiscoveryResponse(std::unique_ptr<envoy::api::v2::DeltaDiscoveryResponse>&& message) override;

protected:
  void establishGrpcStream() override;
  void sendGrpcMessage(void* msg_proto_ptr) override;
  void maybeUpdateQueueSizeStat(uint64_t size) override;
  bool grpcStreamAvailable() const override;
  bool rateLimitAllowsDrain() override;

private:
  GrpcStream<envoy::api::v2::DeltaDiscoveryRequest, envoy::api::v2::DeltaDiscoveryResponse>
      grpc_stream_;
};

class GrpcMuxSotw : public GrpcMuxImpl,
                    public GrpcStreamCallbacks<envoy::api::v2::DiscoveryResponse> {
public:
  GrpcMuxSotw(Grpc::RawAsyncClientPtr&& async_client, Event::Dispatcher& dispatcher,
              const Protobuf::MethodDescriptor& service_method, Runtime::RandomGenerator& random,
              Stats::Scope& scope, const RateLimitSettings& rate_limit_settings,
              const LocalInfo::LocalInfo& local_info, bool skip_subsequent_node);

  // GrpcStreamCallbacks
  void onStreamEstablished() override;
  void onEstablishmentFailure() override;
  void onWriteable() override;
  void onDiscoveryResponse(std::unique_ptr<envoy::api::v2::DiscoveryResponse>&& message) override;
  GrpcStream<envoy::api::v2::DiscoveryRequest, envoy::api::v2::DiscoveryResponse>&
  grpcStreamForTest() {
    return grpc_stream_;
  }

protected:
  void establishGrpcStream() override;
  void sendGrpcMessage(void* msg_proto_ptr) override;
  void maybeUpdateQueueSizeStat(uint64_t size) override;
  bool grpcStreamAvailable() const override;
  bool rateLimitAllowsDrain() override;

private:
  GrpcStream<envoy::api::v2::DiscoveryRequest, envoy::api::v2::DiscoveryResponse> grpc_stream_;
};

class NullGrpcMuxImpl : public GrpcMux {
public:
  void start() override {}

  void pause(const std::string&) override {}
  void resume(const std::string&) override {}
  bool paused(const std::string&) const override { return false; }

  Watch* addOrUpdateWatch(const std::string&, Watch*, const std::set<std::string>&,
                          SubscriptionCallbacks&, std::chrono::milliseconds) override {
    throw EnvoyException("ADS must be configured to support an ADS config source");
  }
  void removeWatch(const std::string&, Watch*) override {
    throw EnvoyException("ADS must be configured to support an ADS config source");
  }
  void disableInitFetchTimeoutTimer() override {}
};

} // namespace Config
} // namespace Envoy
