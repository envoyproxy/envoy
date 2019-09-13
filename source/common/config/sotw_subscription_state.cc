#include "common/config/sotw_subscription_state.h"

#include "common/common/assert.h"
#include "common/common/hash.h"

namespace Envoy {
namespace Config {

SotwSubscriptionState::SotwSubscriptionState(const std::string& type_url,
                                             SubscriptionCallbacks& callbacks,
                                             const LocalInfo::LocalInfo& local_info,
                                             std::chrono::milliseconds init_fetch_timeout,
                                             Event::Dispatcher& dispatcher,
                                             bool skip_subsequent_node)
    : SubscriptionState(type_url, callbacks, local_info, init_fetch_timeout, dispatcher,
                        skip_subsequent_node) {}

SotwSubscriptionState::~SotwSubscriptionState() {}

SotwSubscriptionStateFactory::SotwSubscriptionStateFactory(Event::Dispatcher& dispatcher,
                                                           const LocalInfo::LocalInfo& local_info,
                                                           bool skip_subsequent_node)
    : dispatcher_(dispatcher), local_info_(local_info),
      skip_subsequent_node_(skip_subsequent_node) {}

SotwSubscriptionStateFactory::~SotwSubscriptionStateFactory() {}

std::unique_ptr<SubscriptionState>
SotwSubscriptionStateFactory::makeSubscriptionState(const std::string& type_url,
                                                    SubscriptionCallbacks& callbacks,
                                                    std::chrono::milliseconds init_fetch_timeout) {
  return std::make_unique<SotwSubscriptionState>(
      type_url, callbacks, local_info_, init_fetch_timeout, dispatcher_, skip_subsequent_node_);
}

void SotwSubscriptionState::updateSubscriptionInterest(const std::set<std::string>& cur_added,
                                                       const std::set<std::string>& cur_removed) {
  for (const auto& a : cur_added) {
    names_tracked_.insert(a);
  }
  for (const auto& r : cur_removed) {
    names_tracked_.erase(r);
  }
  update_pending_ = true;
}

// Not having sent any requests yet counts as an "update pending" since you're supposed to resend
// the entirety of your interest at the start of a stream, even if nothing has changed.
bool SotwSubscriptionState::subscriptionUpdatePending() const { return update_pending_; }

UpdateAck SotwSubscriptionState::handleResponse(const void* reponse_proto_ptr) {
  auto* response = static_cast<const envoy::api::v2::DiscoveryResponse*>(reponse_proto_ptr);
  // We *always* copy the response's nonce into the next request, even if we're going to make that
  // request a NACK by setting error_detail.
  UpdateAck ack(response->nonce(), type_url());
  try {
    handleGoodResponse(*response);
  } catch (const EnvoyException& e) {
    handleBadResponse(e, ack);
  }
  return ack;
}

void SotwSubscriptionState::handleGoodResponse(const envoy::api::v2::DiscoveryResponse& message) {
  disableInitFetchTimeoutTimer();
  callbacks().onConfigUpdate(message.resources(), message.version_info());
  // Now that we're passed onConfigUpdate() without an exception thrown, we know we're good.
  last_good_version_info_ = message.version_info();
  ENVOY_LOG(debug, "Config update for {} accepted with {} resources", type_url(),
            message.resources().size());
}

void SotwSubscriptionState::handleBadResponse(const EnvoyException& e, UpdateAck& ack) {
  // Note that error_detail being set is what indicates that a DeltaDiscoveryRequest is a NACK.
  ack.error_detail_.set_code(Grpc::Status::GrpcStatus::Internal);
  ack.error_detail_.set_message(e.what());
  disableInitFetchTimeoutTimer();
  ENVOY_LOG(warn, "Config update for {} rejected: {}", type_url(), e.what());
  callbacks().onConfigUpdateFailed(Envoy::Config::ConfigUpdateFailureReason::UpdateRejected, &e);
}

void SotwSubscriptionState::handleEstablishmentFailure() {
  disableInitFetchTimeoutTimer();
  ENVOY_LOG(debug, "gRPC update for {} failed", type_url());
  callbacks().onConfigUpdateFailed(Envoy::Config::ConfigUpdateFailureReason::ConnectionFailure,
                                   nullptr);
}

envoy::api::v2::DiscoveryRequest* SotwSubscriptionState::getNextRequestInternal() {
  auto* request = new envoy::api::v2::DiscoveryRequest;

  std::copy(names_tracked_.begin(), names_tracked_.end(),
            Protobuf::RepeatedFieldBackInserter(request->mutable_resource_names()));

  request->set_type_url(type_url());
  if (!any_request_sent_yet_in_current_stream_ || !skip_subsequent_node()) {
    request->mutable_node()->MergeFrom(local_info().node());
  }
  any_request_sent_yet_in_current_stream_ = true;
  update_pending_ = false;
  return request;
}

void* SotwSubscriptionState::getNextRequestAckless() { return getNextRequestInternal(); }

void* SotwSubscriptionState::getNextRequestWithAck(const UpdateAck& ack) {
  envoy::api::v2::DiscoveryRequest* request = getNextRequestInternal();
  request->set_response_nonce(ack.nonce_);
  if (last_good_version_info_.has_value()) {
    request->set_version_info(last_good_version_info_.value());
  }
  if (ack.error_detail_.code() != Grpc::Status::GrpcStatus::Ok) {
    // Don't needlessly make the field present-but-empty if status is ok.
    request->mutable_error_detail()->CopyFrom(ack.error_detail_);
  }
  return request;
}

} // namespace Config
} // namespace Envoy
