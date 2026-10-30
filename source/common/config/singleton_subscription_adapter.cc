#include "source/common/config/singleton_subscription_adapter.h"

#include <utility>

#include "source/common/common/assert.h"
#include "source/common/common/fmt.h"
#include "source/common/config/xds_resource.h"

#include "absl/memory/memory.h"

namespace Envoy {
namespace Config {

absl::StatusOr<std::unique_ptr<SingletonSubscriptionCallbacksAdapter>>
SingletonSubscriptionCallbacksAdapter::create(SingletonSubscriptionCallbacks& callbacks,
                                              absl::string_view resource_name) {
  auto normalized_name_or_error = XdsResourceIdentifier::normalizeResourceName(resource_name);
  RETURN_IF_NOT_OK_REF(normalized_name_or_error.status());
  return absl::WrapUnique(new SingletonSubscriptionCallbacksAdapter(
      callbacks, std::move(normalized_name_or_error.value())));
}

SingletonSubscriptionCallbacksAdapter::SingletonSubscriptionCallbacksAdapter(
    SingletonSubscriptionCallbacks& callbacks, std::string resource_name)
    : callbacks_(callbacks), resource_name_(std::move(resource_name)) {}

absl::Status SingletonSubscriptionCallbacksAdapter::onConfigUpdate(
    const std::vector<DecodedResourceRef>& resources, const std::string& version_info) {
  if (resources.empty()) {
    callbacks_.onResourceRemoved();
    return absl::OkStatus();
  }
  if (resources.size() > 1) {
    return absl::InvalidArgumentError(fmt::format(
        "Unexpected multiple resources ({} resources) in singleton SotW update", resources.size()));
  }
  if (resources[0].get().name() != resource_name_) {
    auto normalized_name_or_error =
        XdsResourceIdentifier::normalizeResourceName(resources[0].get().name());
    RETURN_IF_NOT_OK_REF(normalized_name_or_error.status());
    if (normalized_name_or_error.value() != resource_name_) {
      return absl::InvalidArgumentError(
          fmt::format("Unexpected resource name '{}' (expecting '{}') in singleton SotW update",
                      resources[0].get().name(), resource_name_));
    }
  }
  return callbacks_.onResourceUpdate(resources[0].get(), version_info);
}

absl::Status SingletonSubscriptionCallbacksAdapter::onConfigUpdate(
    const std::vector<DecodedResourceRef>& added_resources,
    const Protobuf::RepeatedPtrField<std::string>& removed_resources,
    const std::string& system_version_info) {
  if (!added_resources.empty()) {
    if (added_resources.size() > 1) {
      return absl::InvalidArgumentError(fmt::format(
          "Unexpected multiple added resources ({} resources) in singleton Delta update",
          added_resources.size()));
    }
    if (added_resources[0].get().name() != resource_name_) {
      auto normalized_name_or_error =
          XdsResourceIdentifier::normalizeResourceName(added_resources[0].get().name());
      RETURN_IF_NOT_OK_REF(normalized_name_or_error.status());
      if (normalized_name_or_error.value() != resource_name_) {
        return absl::InvalidArgumentError(
            fmt::format("Unexpected resource name '{}' (expecting '{}') in singleton Delta update",
                        added_resources[0].get().name(), resource_name_));
      }
    }
    return callbacks_.onResourceUpdate(added_resources[0].get(), system_version_info);
  }
  if (!removed_resources.empty()) {
    if (removed_resources.size() > 1) {
      return absl::InvalidArgumentError(fmt::format(
          "Unexpected multiple removed resources ({} resources) in singleton Delta update",
          removed_resources.size()));
    }
    if (removed_resources[0] != resource_name_) {
      auto normalized_name_or_error =
          XdsResourceIdentifier::normalizeResourceName(removed_resources[0]);
      RETURN_IF_NOT_OK_REF(normalized_name_or_error.status());
      if (normalized_name_or_error.value() != resource_name_) {
        return absl::InvalidArgumentError(fmt::format(
            "Unexpected removed resource name '{}' (expecting '{}') in singleton Delta update",
            removed_resources[0], resource_name_));
      }
    }
    callbacks_.onResourceRemoved();
    return absl::OkStatus();
  }
  // Both added_resources and removed_resources are empty (heartbeat / no-op update).
  return absl::OkStatus();
}

void SingletonSubscriptionCallbacksAdapter::onConfigUpdateFailed(ConfigUpdateFailureReason reason,
                                                                 const EnvoyException* e) {
  callbacks_.onFailure(reason, e);
}

SingletonSubscriptionImpl::SingletonSubscriptionImpl(
    SubscriptionPtr sub, absl::string_view resource_name,
    std::unique_ptr<SingletonSubscriptionCallbacksAdapter> adapter)
    : adapter_(std::move(adapter)), sub_(std::move(sub)), resource_name_(resource_name) {}

void SingletonSubscriptionImpl::start() { sub_->start({resource_name_}); }

} // namespace Config
} // namespace Envoy
