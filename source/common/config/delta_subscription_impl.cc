#include "common/config/delta_subscription_impl.h"

namespace Envoy {
namespace Config {

DeltaSubscriptionImpl::DeltaSubscriptionImpl(GrpcMuxSharedPtr context, absl::string_view type_url,
                                             SubscriptionCallbacks& callbacks,
                                             SubscriptionStats stats,
                                             std::chrono::milliseconds init_fetch_timeout,
                                             bool is_aggregated)
    : context_(std::move(context)), type_url_(type_url), callbacks_(callbacks), stats_(stats),
      init_fetch_timeout_(init_fetch_timeout), is_aggregated_(is_aggregated) {}

DeltaSubscriptionImpl::~DeltaSubscriptionImpl() {
  if (watch_) {
    context_->removeWatch(type_url_, watch_);
  }
}

void DeltaSubscriptionImpl::pause() { context_->pause(type_url_); }

void DeltaSubscriptionImpl::resume() { context_->resume(type_url_); }

// Config::DeltaSubscription
void DeltaSubscriptionImpl::start(const std::set<std::string>& resources) {
  // ADS initial request batching relies on the users of the GrpcMux *not* calling start on it,
  // whereas non-ADS xDS users must call it themselves.
  if (!is_aggregated_) {
    context_->start();
  }
  watch_ = context_->addOrUpdateWatch(type_url_, watch_, resources, *this, init_fetch_timeout_);
  stats_.update_attempt_.inc();
}

void DeltaSubscriptionImpl::updateResourceInterest(
    const std::set<std::string>& update_to_these_names) {
  watch_ = context_->addOrUpdateWatch(type_url_, watch_, update_to_these_names, *this,
                                      init_fetch_timeout_);
  stats_.update_attempt_.inc();
}

// Config::SubscriptionCallbacks
void DeltaSubscriptionImpl::onConfigUpdate(
    const Protobuf::RepeatedPtrField<ProtobufWkt::Any>& resources,
    const std::string& version_info) {
  stats_.update_attempt_.inc();
  callbacks_.onConfigUpdate(resources, version_info);
  stats_.update_success_.inc();
  stats_.version_.set(HashUtil::xxHash64(version_info));
}

void DeltaSubscriptionImpl::onConfigUpdate(
    const Protobuf::RepeatedPtrField<envoy::api::v2::Resource>& added_resources,
    const Protobuf::RepeatedPtrField<std::string>& removed_resources,
    const std::string& system_version_info) {
  stats_.update_attempt_.inc();
  callbacks_.onConfigUpdate(added_resources, removed_resources, system_version_info);
  stats_.update_success_.inc();
  stats_.version_.set(HashUtil::xxHash64(system_version_info));
}

void DeltaSubscriptionImpl::onConfigUpdateFailed(ConfigUpdateFailureReason reason,
                                                 const EnvoyException* e) {
  switch (reason) {
  case Envoy::Config::ConfigUpdateFailureReason::ConnectionFailure:
    // This is a gRPC-stream-level establishment failure, not an xDS-protocol-level failure.
    // So, don't onConfigUpdateFailed() here. Instead, allow a retry of the gRPC stream.
    // If init_fetch_timeout_ is non-zero, the server will continue startup after that timeout.
    stats_.update_failure_.inc();
    // TODO(fredlas) remove; it only makes sense to count start() and updateResourceInterest()
    // as attempts.
    stats_.update_attempt_.inc();
    break;
  case Envoy::Config::ConfigUpdateFailureReason::FetchTimedout:
    stats_.init_fetch_timeout_.inc();
    // TODO(fredlas) remove; it only makes sense to count start() and updateResourceInterest()
    // as attempts.
    stats_.update_attempt_.inc();
    callbacks_.onConfigUpdateFailed(reason, e);
    break;
  case Envoy::Config::ConfigUpdateFailureReason::UpdateRejected:
    // We expect Envoy exception to be thrown when update is rejected.
    ASSERT(e != nullptr);
    stats_.update_rejected_.inc();
    callbacks_.onConfigUpdateFailed(reason, e);
    break;
  }
}

std::string DeltaSubscriptionImpl::resourceName(const ProtobufWkt::Any& resource) {
  return callbacks_.resourceName(resource);
}

} // namespace Config
} // namespace Envoy
