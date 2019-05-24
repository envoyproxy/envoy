#include "common/config/delta_subscription_impl.h"

namespace Envoy {
namespace Config {

DeltaSubscriptionImpl::DeltaSubscriptionImpl(std::shared_ptr<GrpcMux> context,
                                             absl::string_view type_url, SubscriptionStats stats,
                                             std::chrono::milliseconds init_fetch_timeout)
    : context_(context), type_url_(type_url), stats_(stats),
      init_fetch_timeout_(init_fetch_timeout) {}

DeltaSubscriptionImpl::~DeltaSubscriptionImpl() { context_->removeSubscription(type_url_); }

void DeltaSubscriptionImpl::pause() { context_->pause(type_url_); }

void DeltaSubscriptionImpl::resume() { context_->resume(type_url_); }

// Config::DeltaSubscription
void DeltaSubscriptionImpl::start(const std::set<std::string>& resources,
                                  SubscriptionCallbacks& callbacks) {
  context_->addSubscription(resources, type_url_, callbacks, stats_, init_fetch_timeout_);
}

void DeltaSubscriptionImpl::updateResources(const std::set<std::string>& update_to_these_names) {
  context_->updateResources(update_to_these_names, type_url_);
}

} // namespace Config
} // namespace Envoy
