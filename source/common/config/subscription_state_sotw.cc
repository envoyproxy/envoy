#include "common/common/assert.h"
#include "common/common/hash.h"
#include "common/config/sotw_subscription_state.h"

namespace Envoy {
namespace Config {

SubscriptionStateSotw::SubscriptionStateSotw(const std::string& type_url,
                                             SubscriptionCallbacks& callbacks,
                                             const LocalInfo::LocalInfo& local_info,
                                             std::chrono::milliseconds init_fetch_timeout,
                                             Event::Dispatcher& dispatcher)
    : type_url_(type_url), callbacks_(callbacks), local_info_(local_info),
      init_fetch_timeout_(init_fetch_timeout) {
  setInitFetchTimeout(dispatcher);
}

void SubscriptionStateSotw::setInitFetchTimeout(Event::Dispatcher& dispatcher) {
  if (init_fetch_timeout_.count() > 0 && !init_fetch_timeout_timer_) {
    init_fetch_timeout_timer_ = dispatcher.createTimer([this]() -> void {
      ENVOY_LOG(warn, "delta config: initial fetch timed out for {}", type_url_);
      callbacks_.onConfigUpdateFailed(nullptr);
    });
    init_fetch_timeout_timer_->enableTimer(init_fetch_timeout_);
  }
}

void SubscriptionStateSotw::pause() {
  ENVOY_LOG(debug, "Pausing discovery requests for {}", type_url_);
  ASSERT(!paused_);
  paused_ = true;
}

void SubscriptionStateSotw::resume() {
  ENVOY_LOG(debug, "Resuming discovery requests for {}", type_url_);
  ASSERT(paused_);
  paused_ = false;
}

void SubscriptionStateSotw::updateSubscriptionInterest(const std::set<std::string>& cur_added,
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
bool SubscriptionStateSotw::subscriptionUpdatePending() const { return update_pending_; }

UpdateAck SubscriptionStateSotw::handleResponse(const envoy::api::v2::DiscoveryResponse& message) {
  // We *always* copy the response's nonce into the next request, even if we're going to make that
  // request a NACK by setting error_detail.
  UpdateAck ack(message.nonce(), type_url_);
  try {
    handleGoodResponse(message);
  } catch (const EnvoyException& e) {
    handleBadResponse(e, ack);
  }
  return ack;
}

void SubscriptionStateSotw::handleGoodResponse(const envoy::api::v2::DiscoveryResponse& message) {
  disableInitFetchTimeoutTimer();
  callbacks_.onConfigUpdate(message.resources(), message.version_info());
  // Now that we're passed onConfigUpdate() without an exception thrown, we know we're good.
  last_good_version_info_ = message.version_info();
  ENVOY_LOG(debug, "Config update for {} accepted with {} resources", type_url_,
            message.resources().size());
}

void SubscriptionStateSotw::handleBadResponse(const EnvoyException& e, UpdateAck& ack) {
  // Note that error_detail being set is what indicates that a DeltaDiscoveryRequest is a NACK.
  ack.error_detail_.set_code(Grpc::Status::GrpcStatus::Internal);
  ack.error_detail_.set_message(e.what());
  disableInitFetchTimeoutTimer();
  ENVOY_LOG(warn, "Config update for {} rejected: {}", type_url_, e.what());
  callbacks_.onConfigUpdateFailed(&e);
}

void SubscriptionStateSotw::handleEstablishmentFailure() {
  disableInitFetchTimeoutTimer();
  ENVOY_LOG(debug, "gRPC update for {} failed", type_url_);
  callbacks_.onConfigUpdateFailed(nullptr);
}

envoy::api::v2::DiscoveryRequest SubscriptionStateSotw::getNextRequestAckless() {
  envoy::api::v2::DiscoveryRequest request;

  std::copy(names_tracked_.begin(), names_tracked_.end(),
            Protobuf::RepeatedFieldBackInserter(request.mutable_resource_names()));

  request.set_type_url(type_url_);
  request.mutable_node()->MergeFrom(local_info_.node());
  update_pending_ = false;
  return request;
}

envoy::api::v2::DiscoveryRequest
SubscriptionStateSotw::getNextRequestWithAck(const UpdateAck& ack) {
  envoy::api::v2::DiscoveryRequest request = getNextRequestAckless();
  request.set_response_nonce(ack.nonce_);
  if (ack.error_detail_.code() != Grpc::Status::GrpcStatus::Ok) {
    // Don't needlessly make the field present-but-empty if status is ok.
    request.mutable_error_detail()->CopyFrom(ack.error_detail_);
    // TODO TODO? if last_good_version_info_ != nullopt
    request.set_version_info(last_good_version_info_);
  } else {
    // TODO TODO in SotW, seems like previous response's version_info should accompany the
    // response_nonce....
    //         ........ hmmmm..... i think everything is fine if we can add a version_info field to
    //         UpdateAck (even though unusued for delta)
    request.set_version_info(ack.version_info_)
  }
  return request;
}

void SubscriptionStateSotw::disableInitFetchTimeoutTimer() {
  if (init_fetch_timeout_timer_) {
    init_fetch_timeout_timer_->disableTimer();
    init_fetch_timeout_timer_.reset();
  }
}

class SubscriptionStateSotw : public Logger::Loggable<Logger::Id::config> {
  const std::string type_url_;
  // callbacks_ is expected to be a WatchMap.
  SubscriptionCallbacks& callbacks_;
  const LocalInfo::LocalInfo& local_info_;
  std::chrono::milliseconds init_fetch_timeout_;
  Event::TimerPtr init_fetch_timeout_timer_;
  std::string last_good_version_info_; // should this be absl::optional to distinguish none and ""?

  bool paused_{};
  bool update_pending_{true}; // Should send a request upon subscription start.

  SubscriptionStateSotw(const SubscriptionStateSotw&) = delete;
  SubscriptionStateSotw& operator=(const SubscriptionStateSotw&) = delete;
};

} // namespace Config
} // namespace Envoy
