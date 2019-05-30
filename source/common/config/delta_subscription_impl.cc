#include "common/config/delta_subscription_impl.h"

namespace Envoy {
namespace Config {

DeltaSubscriptionImpl::DeltaSubscriptionImpl(std::shared_ptr<GrpcMux> context,
                                             absl::string_view type_url, SubscriptionStats stats,
                                             std::chrono::milliseconds init_fetch_timeout)
    : context_(context), type_url_(type_url), stats_(stats) {
  context_->setInitFetchTimeout(type_url_, init_fetch_timeout);
}

DeltaSubscriptionImpl::~DeltaSubscriptionImpl() {
  if (watch_token_ != WatchMap::InvalidToken) {
    context_->removeWatch(type_url_, watch_token_);
  }
}

void DeltaSubscriptionImpl::pause() { context_->pause(type_url_); }

void DeltaSubscriptionImpl::resume() { context_->resume(type_url_); }

// Config::DeltaSubscription
void DeltaSubscriptionImpl::start(const std::set<std::string>& resources,
                                  SubscriptionCallbacks& callbacks) {
  watch_token_ = context_->addWatch(type_url_, resources, callbacks, stats_);
}

void DeltaSubscriptionImpl::updateResources(const std::set<std::string>& update_to_these_names) {
  context_->updateWatch(type_url_, watch_token_, update_to_these_names);
}

} // namespace Config
} // namespace Envoy
