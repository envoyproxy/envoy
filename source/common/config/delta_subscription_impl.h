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

/**
 * Manages the logic of a (non-aggregated) delta xDS subscription.
 * TODO(fredlas) add aggregation support. The plan is for that to happen in XdsGrpcContext,
 *               which this class will then "have a" rather than "be a".
 */
class DeltaSubscriptionImpl : public Subscription,
                              public GrpcStreamCallbacks<envoy::api::v2::DeltaDiscoveryResponse>,
                              public Logger::Loggable<Logger::Id::config> {
public:
  DeltaSubscriptionImpl(const LocalInfo::LocalInfo& local_info, Grpc::AsyncClientPtr async_client,
                        Event::Dispatcher& dispatcher,
                        const Protobuf::MethodDescriptor& service_method,
                        absl::string_view type_url, Runtime::RandomGenerator& random,
                        Stats::Scope& scope, const RateLimitSettings& rate_limit_settings,
                        SubscriptionStats stats, std::chrono::milliseconds init_fetch_timeout)
      : grpc_stream_(this, std::move(async_client), service_method, random, dispatcher, scope,
                     rate_limit_settings),
        type_url_(type_url), local_info_(local_info), stats_(stats), dispatcher_(dispatcher),
        init_fetch_timeout_(init_fetch_timeout) {}

  void pause() { state_->pause(); }
  void resume() {
    state_->resume();
    trySendDiscoveryRequests();
  }

  // Config::Subscription
  void start(const std::set<std::string>& resources, SubscriptionCallbacks& callbacks) override {
    state_ = std::make_unique<DeltaSubscriptionState>(type_url_, resources, callbacks, local_info_,
                                                      init_fetch_timeout_, dispatcher_, stats_);
    grpc_stream_.establishNewStream();
    updateResources(resources);
  }

  void updateResources(const std::set<std::string>& update_to_these_names) override {
    state_->updateResourceInterest(update_to_these_names);
    // Tell the server about our new interests, if there are any.
    trySendDiscoveryRequests();
  }

  // Config::GrpcStreamCallbacks
  void onStreamEstablished() override {
    state_->markStreamFresh();
    trySendDiscoveryRequests();
  }

  void onEstablishmentFailure() override { state_->handleEstablishmentFailure(); }

  void
  onDiscoveryResponse(std::unique_ptr<envoy::api::v2::DeltaDiscoveryResponse>&& message) override {
    ENVOY_LOG(debug, "Received gRPC message for {} at version {}", type_url_,
              message->system_version_info());
    kickOffAck(state_->handleResponse(*message));
  }

  void onWriteable() override { trySendDiscoveryRequests(); }

private:
  void kickOffAck(UpdateAck ack) {
    ack_queue_.push(ack);
    trySendDiscoveryRequests();
  }

  // Checks whether external conditions allow sending a DeltaDiscoveryRequest. (Does not check
  // whether we *want* to send a DeltaDiscoveryRequest).
  bool canSendDiscoveryRequest() {
    if (state_->paused()) {
      ENVOY_LOG(trace, "API {} paused; discovery request on hold for now.", type_url_);
      return false;
    } else if (!grpc_stream_.grpcStreamAvailable()) {
      ENVOY_LOG(trace, "No stream available to send a DiscoveryRequest for {}.", type_url_);
      return false;
    } else if (!grpc_stream_.checkRateLimitAllowsDrain()) {
      ENVOY_LOG(trace, "{} DiscoveryRequest hit rate limit; will try later.", type_url_);
      return false;
    }
    return true;
  }

  // Checks whether we have something to say in a DeltaDiscoveryRequest, which can be an ACK and/or
  // a subscription update. (Does not check whether we *can* send a DeltaDiscoveryRequest).
  bool wantToSendDiscoveryRequest() {
    return !ack_queue_.empty() || state_->subscriptionUpdatePending();
  }

  void trySendDiscoveryRequests() {
    while (wantToSendDiscoveryRequest() && canSendDiscoveryRequest()) {
      envoy::api::v2::DeltaDiscoveryRequest request = state_->getNextRequest();
      if (!ack_queue_.empty()) {
        const UpdateAck& ack = ack_queue_.front();
        request.set_response_nonce(ack.nonce_);
        if (ack.error_detail_.code() != Grpc::Status::GrpcStatus::Ok) {
          // Don't needlessly make the field present-but-empty if status is ok.
          request.mutable_error_detail()->CopyFrom(ack.error_detail_);
        }
        ack_queue_.pop();
      }
      ENVOY_LOG(trace, "Sending DiscoveryRequest for {}: {}", type_url_, request.DebugString());
      grpc_stream_.sendMessage(request);
    }
    grpc_stream_.maybeUpdateQueueSizeStat(ack_queue_.size());
  }

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
  SubscriptionStats stats_;
  Event::Dispatcher& dispatcher_;
  std::chrono::milliseconds init_fetch_timeout_;

  std::unique_ptr<DeltaSubscriptionState> state_;
};

} // namespace Config
} // namespace Envoy
