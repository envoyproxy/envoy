#include "common/config/grpc_subscription_impl.h"

#include "common/common/assert.h"
#include "common/common/logger.h"
#include "common/grpc/common.h"
#include "common/protobuf/protobuf.h"
#include "common/protobuf/utility.h"

namespace Envoy {
namespace Config {

GrpcSubscriptionImpl::GrpcSubscriptionImpl(GrpcMuxSharedPtr grpc_mux,
                                           SubscriptionCallbacks& callbacks,
                                           SubscriptionStats stats, absl::string_view type_url,
                                           Event::Dispatcher& dispatcher,
                                           std::chrono::milliseconds init_fetch_timeout,
                                           bool is_aggregated)
    : grpc_mux_(grpc_mux), callbacks_(callbacks), stats_(stats), type_url_(type_url),
      dispatcher_(dispatcher), init_fetch_timeout_(init_fetch_timeout),
      is_aggregated_(is_aggregated) {}

// Config::Subscription
void GrpcSubscriptionImpl::start(const std::set<std::string>& resources) {
  if (init_fetch_timeout_.count() > 0) {
    init_fetch_timeout_timer_ = dispatcher_.createTimer([this]() -> void {
      callbacks_.onConfigUpdateFailed(Envoy::Config::ConfigUpdateFailureReason::FetchTimedout,
                                      nullptr);
    });
    init_fetch_timeout_timer_->enableTimer(init_fetch_timeout_);
  }

  watch_ = grpc_mux_->subscribe(type_url_, resources, *this);
  // The attempt stat here is maintained for the purposes of having consistency between ADS and
  // gRPC/filesystem/REST Subscriptions. Since ADS is push based and muxed, the notion of an
  // "attempt" for a given xDS API combined by ADS is not really that meaningful.
  stats_.update_attempt_.inc();

  if (!is_aggregated_) {
    grpc_mux_->start();
  }
}

void GrpcSubscriptionImpl::updateResourceInterest(
    const std::set<std::string>& update_to_these_names) {
  // First destroy the watch, so that this subscribe doesn't send a request for both the
  // previously watched resources and the new ones (we may have lost interest in some of the
  // previously watched ones).
  watch_.reset();
  watch_ = grpc_mux_->subscribe(type_url_, update_to_these_names, *this);
  stats_.update_attempt_.inc();
}

// Config::SubscriptionCallbacks
void GrpcSubscriptionImpl::onConfigUpdate(
    const Protobuf::RepeatedPtrField<ProtobufWkt::Any>& resources,
    const std::string& version_info) {
  disableInitFetchTimeoutTimer();
  // TODO(mattklein123): In the future if we start tracking per-resource versions, we need to
  // supply those versions to onConfigUpdate() along with the xDS response ("system")
  // version_info. This way, both types of versions can be tracked and exposed for debugging by
  // the configuration update targets.
  callbacks_.onConfigUpdate(resources, version_info);
  stats_.update_success_.inc();
  stats_.update_attempt_.inc();
  stats_.version_.set(HashUtil::xxHash64(version_info));
  ENVOY_LOG(debug, "gRPC config for {} accepted with {} resources with version {}", type_url_,
            resources.size(), version_info);
}

void GrpcSubscriptionImpl::onConfigUpdate(
    const Protobuf::RepeatedPtrField<envoy::service::discovery::v3::Resource>&,
    const Protobuf::RepeatedPtrField<std::string>&, const std::string&) {
  // TODO(bgallagher): This is a broken abstraction. The issue is that there used to be 2 different
  // callback interfaces, one for sotw and one for delta. Now they're merged. This method is the
  // delta method. We also have 2 different classes that implement this interface. This class is the
  // sotw implementation. It should never receive this callback. The next step will be to merge the
  // DeltaSubscriptionImpl and GrpcSubscriptionImpl classes at which time this code will be needed.
  // I think that adding the assert here is the easiest way to move forward without ending up with a
  // massive change.
  NOT_REACHED_GCOVR_EXCL_LINE;
}

void GrpcSubscriptionImpl::onConfigUpdateFailed(ConfigUpdateFailureReason reason,
                                                const EnvoyException* e) {
  switch (reason) {
  case Envoy::Config::ConfigUpdateFailureReason::ConnectionFailure:
    stats_.update_failure_.inc();
    ENVOY_LOG(debug, "gRPC update for {} failed", type_url_);
    break;
  case Envoy::Config::ConfigUpdateFailureReason::FetchTimedout:
    stats_.init_fetch_timeout_.inc();
    disableInitFetchTimeoutTimer();
    ENVOY_LOG(warn, "gRPC config: initial fetch timed out for {}", type_url_);
    break;
  case Envoy::Config::ConfigUpdateFailureReason::UpdateRejected:
    // We expect Envoy exception to be thrown when update is rejected.
    ASSERT(e != nullptr);
    disableInitFetchTimeoutTimer();
    stats_.update_rejected_.inc();
    ENVOY_LOG(warn, "gRPC config for {} rejected: {}", type_url_, e->what());
    break;
  }

  stats_.update_attempt_.inc();
  if (reason == Envoy::Config::ConfigUpdateFailureReason::ConnectionFailure) {
    // New gRPC stream will be established and send requests again.
    // If init_fetch_timeout is non-zero, server will continue startup after it timeout
    return;
  }

  callbacks_.onConfigUpdateFailed(reason, e);
}

std::string GrpcSubscriptionImpl::resourceName(const ProtobufWkt::Any& resource) {
  return callbacks_.resourceName(resource);
}

void GrpcSubscriptionImpl::disableInitFetchTimeoutTimer() {
  if (init_fetch_timeout_timer_) {
    init_fetch_timeout_timer_->disableTimer();
    init_fetch_timeout_timer_.reset();
  }
}

} // namespace Config
} // namespace Envoy
