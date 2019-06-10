#pragma once

#include <queue>

#include "envoy/api/v2/discovery.pb.h"
#include "envoy/common/token_bucket.h"
#include "envoy/config/subscription.h"
#include "envoy/config/xds_grpc_context.h"
#include "envoy/local_info/local_info.h"

#include "common/common/logger.h"
#include "common/config/delta_subscription_state.h"
#include "common/config/grpc_stream.h"
#include "common/grpc/common.h"

namespace Envoy {
namespace Config {

/**
 * Manages the logic of a (non-aggregated) delta xDS subscription.
 * TODO(fredlas) add aggregation support. The plan is for that to happen in XdsGrpcContext,
 *               which this class will then "have a" rather than "be a".
 */
class DeltaSubscriptionImpl : public Subscription,
                              public GrpcStreamCallbacks<envoy::api::v2::DeltaDiscoveryResponse>,
                              public Logger::Loggable<Logger::Id::config> {
public:
  DeltaSubscriptionImpl(const LocalInfo::LocalInfo& local_info,
                        Grpc::RawAsyncClientPtr async_client, Event::Dispatcher& dispatcher,
                        const Protobuf::MethodDescriptor& service_method,
                        absl::string_view type_url, Runtime::RandomGenerator& random,
                        Stats::Scope& scope, const RateLimitSettings& rate_limit_settings,
                        SubscriptionCallbacks& callbacks, SubscriptionStats stats,
                        std::chrono::milliseconds init_fetch_timeout);

  void pause();
  void resume();

  // Config::Subscription
  void start(const std::set<std::string>& resource_names) override;
  void updateResources(const std::set<std::string>& update_to_these_names) override;

  // Config::GrpcStreamCallbacks
  void onStreamEstablished() override;
  void onEstablishmentFailure() override;
  void
  onDiscoveryResponse(std::unique_ptr<envoy::api::v2::DeltaDiscoveryResponse>&& message) override;

  void onWriteable() override;

private:
  void kickOffAck(UpdateAck ack);

  // Checks whether external conditions allow sending a DeltaDiscoveryRequest. (Does not check
  // whether we *want* to send a DeltaDiscoveryRequest).
  bool canSendDiscoveryRequest();

  // Checks whether we have something to say in a DeltaDiscoveryRequest, which can be an ACK and/or
  // a subscription update. (Does not check whether we *can* send a DeltaDiscoveryRequest).
  bool wantToSendDiscoveryRequest();

  void trySendDiscoveryRequests();

  GrpcStream<envoy::api::v2::DeltaDiscoveryRequest, envoy::api::v2::DeltaDiscoveryResponse>
      grpc_stream_;

  const std::string type_url_;

  // An item in the queue represents a DeltaDiscoveryRequest that must be sent. If an item is not
  // empty, it is the ACK (nonce + error_detail) to set on that request. An empty entry should
  // still send a request; it just won't have an ACK.
  //
  // More details: DeltaDiscoveryRequest plays two independent roles:
  // 1) informing the server of what resources we're interested in, and
  // 2) acknowledging resources the server has sent us.
  // Each entry in this queue was added for exactly one of those purposes, but since the
  // subscription interest is tracked separately, in a non-queue way, subscription changes can get
  // mixed in with an ACK request. In that case, the entry that the subscription change originally
  // queued up *does* still get sent, just empty and pointless. (TODO(fredlas) we would like to skip
  // those no-op requests).
  std::queue<UpdateAck> ack_queue_;

  const LocalInfo::LocalInfo& local_info_;
  SubscriptionCallbacks& callbacks_;
  SubscriptionStats stats_;
  Event::Dispatcher& dispatcher_;
  std::chrono::milliseconds init_fetch_timeout_;

  std::unique_ptr<DeltaSubscriptionState> state_;
};

} // namespace Config
} // namespace Envoy
