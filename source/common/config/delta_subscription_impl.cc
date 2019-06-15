#include "common/config/delta_subscription_impl.h"

namespace Envoy {
namespace Config {

DeltaSubscriptionImpl::DeltaSubscriptionImpl(
    std::shared_ptr<GrpcMux> context, absl::string_view type_url, SubscriptionCallbacks& callbacks,
    SubscriptionStats stats, std::chrono::milliseconds init_fetch_timeout, bool is_aggregated)
    : context_(context), type_url_(type_url), callbacks_(callbacks), stats_(stats),
      init_fetch_timeout_(init_fetch_timeout), is_aggregated_(is_aggregated) {}

DeltaSubscriptionImpl::~DeltaSubscriptionImpl() {
  if (watch_) {
    // The Watch destruction process assumes all resource interest has already been removed. Doing
    // that cleanly needs the context's involvement. So, do that now, just before destroying the
    // watch.
    context_->addOrUpdateWatch(type_url_, watch_, {}, *this, init_fetch_timeout_);
    // A Watch must not outlive its owning WatchMap. Since DeltaSubscriptionImpl holds a shared_ptr
    // to the context, which owns the WatchMap, the watch must be destroyed first in case this
    // subscription holds the last shared_ptr.
    watch_.reset();
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
  context_->addOrUpdateWatch(type_url_, watch_, resources, *this, init_fetch_timeout_);
  stats_.update_attempt_.inc();
}

void DeltaSubscriptionImpl::updateResourceInterest(
    const std::set<std::string>& update_to_these_names) {
  context_->addOrUpdateWatch(type_url_, watch_, update_to_these_names, *this, init_fetch_timeout_);
  stats_.update_attempt_.inc();
}

// Config::SubscriptionCallbacks
void DeltaSubscriptionImpl::onConfigUpdate(
    const Protobuf::RepeatedPtrField<ProtobufWkt::Any>& resources,
    const std::string& version_info) {
  callbacks_.onConfigUpdate(resources, version_info);
  stats_.update_success_.inc();
  stats_.update_attempt_.inc();
  stats_.version_.set(HashUtil::xxHash64(version_info));
}
void DeltaSubscriptionImpl::onConfigUpdate(
    const Protobuf::RepeatedPtrField<envoy::api::v2::Resource>& added_resources,
    const Protobuf::RepeatedPtrField<std::string>& removed_resources,
    const std::string& system_version_info) {
  callbacks_.onConfigUpdate(added_resources, removed_resources, system_version_info);
  stats_.update_success_.inc();
  stats_.update_attempt_.inc();
  stats_.version_.set(HashUtil::xxHash64(system_version_info));
}
void DeltaSubscriptionImpl::onConfigUpdateFailed(const EnvoyException* e) {
  stats_.update_attempt_.inc();
  // TODO(htuch): Less fragile signal that this is failure vs. reject.
  if (e == nullptr) {
    stats_.update_failure_.inc();
  } else {
    stats_.update_rejected_.inc();
  }
  callbacks_.onConfigUpdateFailed(e);
}
std::string DeltaSubscriptionImpl::resourceName(const ProtobufWkt::Any& resource) {
  return callbacks_.resourceName(resource);
}

} // namespace Config
} // namespace Envoy
