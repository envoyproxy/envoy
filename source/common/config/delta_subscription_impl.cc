#include "common/config/delta_subscription_impl.h"

#include "common/common/assert.h"
#include "common/common/backoff_strategy.h"
#include "common/common/token_bucket_impl.h"
#include "common/config/utility.h"
#include "common/protobuf/protobuf.h"
#include "common/protobuf/utility.h"

namespace Envoy {
namespace Config {

DeltaSubscriptionImpl::DeltaSubscriptionImpl(
    const LocalInfo::LocalInfo& local_info, Grpc::RawAsyncClientPtr async_client,
    Event::Dispatcher& dispatcher, const Protobuf::MethodDescriptor& service_method,
    absl::string_view type_url, Runtime::RandomGenerator& random, Stats::Scope& scope,
    const RateLimitSettings& rate_limit_settings, SubscriptionCallbacks& callbacks,
    SubscriptionStats stats, std::chrono::milliseconds init_fetch_timeout)
    : grpc_stream_(this, std::move(async_client), service_method, random, dispatcher, scope,
                   rate_limit_settings),
      type_url_(type_url), local_info_(local_info), callbacks_(callbacks), stats_(stats),
      dispatcher_(dispatcher), init_fetch_timeout_(init_fetch_timeout) {}

void DeltaSubscriptionImpl::pause() { state_->pause(); }
void DeltaSubscriptionImpl::resume() {
  state_->resume();
  trySendDiscoveryRequests();
}

// Config::Subscription
void DeltaSubscriptionImpl::start(const std::set<std::string>& resource_names) {
  state_ = std::make_unique<DeltaSubscriptionState>(
      type_url_, resource_names, callbacks_, local_info_, init_fetch_timeout_, dispatcher_, stats_);
  grpc_stream_.establishNewStream();
  updateResources(resource_names);
}

void DeltaSubscriptionImpl::updateResources(const std::set<std::string>& update_to_these_names) {
  state_->updateResourceInterest(update_to_these_names);
  // Tell the server about our new interests, if there are any.
  trySendDiscoveryRequests();
}

// Config::GrpcStreamCallbacks
void DeltaSubscriptionImpl::onStreamEstablished() {
  state_->markStreamFresh();
  trySendDiscoveryRequests();
}

void DeltaSubscriptionImpl::onEstablishmentFailure() { state_->handleEstablishmentFailure(); }

void DeltaSubscriptionImpl::onDiscoveryResponse(
    std::unique_ptr<envoy::api::v2::DeltaDiscoveryResponse>&& message) {
  ENVOY_LOG(debug, "Received gRPC message for {} at version {}", type_url_,
            message->system_version_info());
  kickOffAck(state_->handleResponse(*message));
}

void DeltaSubscriptionImpl::onWriteable() { trySendDiscoveryRequests(); }

void DeltaSubscriptionImpl::kickOffAck(UpdateAck ack) {
  ack_queue_.push(ack);
  trySendDiscoveryRequests();
}

// Checks whether external conditions allow sending a DeltaDiscoveryRequest. (Does not check
// whether we *want* to send a DeltaDiscoveryRequest).
bool DeltaSubscriptionImpl::canSendDiscoveryRequest() {
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
bool DeltaSubscriptionImpl::wantToSendDiscoveryRequest() {
  return !ack_queue_.empty() || state_->subscriptionUpdatePending();
}

void DeltaSubscriptionImpl::trySendDiscoveryRequests() {
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

} // namespace Config
} // namespace Envoy
