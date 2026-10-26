#include "source/common/config/dependent_type_pausing_subscription.h"

#include "envoy/common/exception.h"

#include "source/common/config/dependent_type_urls.h"

namespace Envoy {
namespace Config {

absl::StatusOr<SubscriptionPtr>
DependentTypePausingSubscription::create(XdsManager& xds_manager, absl::string_view type_url,
                                         SubscriptionCallbacks& callbacks,
                                         SubscriptionCreator create_subscription) {
  const std::vector<std::string>& dependent_type_urls = dependentTypeUrls(type_url);
  if (dependent_type_urls.empty()) {
    return create_subscription(callbacks);
  }

  auto pausing_subscription = std::unique_ptr<DependentTypePausingSubscription>(
      new DependentTypePausingSubscription(xds_manager, callbacks, dependent_type_urls));
  auto subscription_or_error = create_subscription(*pausing_subscription);
  RETURN_IF_NOT_OK(subscription_or_error.status());
  pausing_subscription->subscription_ = std::move(subscription_or_error.value());
  return pausing_subscription;
}

absl::Status
DependentTypePausingSubscription::onConfigUpdate(const std::vector<DecodedResourceRef>& resources,
                                                 const std::string& version_info) {
  ScopedResume resume = xds_manager_.pause(dependent_type_urls_);
  return callbacks_.onConfigUpdate(resources, version_info);
}

absl::Status DependentTypePausingSubscription::onConfigUpdate(
    const std::vector<DecodedResourceRef>& added_resources,
    const Protobuf::RepeatedPtrField<std::string>& removed_resources,
    const std::string& system_version_info) {
  ScopedResume resume = xds_manager_.pause(dependent_type_urls_);
  return callbacks_.onConfigUpdate(added_resources, removed_resources, system_version_info);
}

} // namespace Config
} // namespace Envoy
