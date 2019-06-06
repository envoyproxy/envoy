#include "common/config/delta_subscription_impl.h"

namespace Envoy {
namespace Config {

DeltaSubscriptionImpl::DeltaSubscriptionImpl(std::shared_ptr<GrpcMux> context,
                                             absl::string_view type_url,
                                             SubscriptionCallbacks& callbacks,
                                             SubscriptionStats stats,
                                             std::chrono::milliseconds init_fetch_timeout)
    : context_(context), type_url_(type_url), callbacks_(callbacks), stats_(stats),
      init_fetch_timeout_(init_fetch_timeout) {}

DeltaSubscriptionImpl::~DeltaSubscriptionImpl() { context_->removeWatch(type_url_, watch_token_); }

void DeltaSubscriptionImpl::pause() { context_->pause(type_url_); }

void DeltaSubscriptionImpl::resume() { context_->resume(type_url_); }

// Config::DeltaSubscription
void DeltaSubscriptionImpl::start(const std::set<std::string>& resources) {
  // TODO TODO needs to be idempotent. GrpcSubscription is special-purposed to non-ADS, and so knows
  // that it needs to call start. My DeltaSubscriptionImpl can be either ADS or not, and so needs to
  // (safely) call start regardless.
  context_->start();

  watch_token_ = context_->addWatch(type_url_, resources, *this, init_fetch_timeout_);
  stats_.update_attempt_.inc();
}

void DeltaSubscriptionImpl::updateResources(const std::set<std::string>& update_to_these_names) {
  context_->updateWatch(type_url_, watch_token_, update_to_these_names);
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
