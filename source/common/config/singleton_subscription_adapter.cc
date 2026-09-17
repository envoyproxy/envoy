#include "source/common/config/singleton_subscription_adapter.h"

#include <utility>

#include "source/common/common/assert.h"
#include "source/common/common/fmt.h"

namespace Envoy {
namespace Config {

SingletonSubscriptionCallbacksAdapter::SingletonSubscriptionCallbacksAdapter(
    SingletonSubscriptionCallbacks& callbacks)
    : callbacks_(callbacks) {}

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
    return callbacks_.onResourceUpdate(added_resources[0].get(), system_version_info);
  }
  if (!removed_resources.empty()) {
    if (removed_resources.size() > 1) {
      return absl::InvalidArgumentError(fmt::format(
          "Unexpected multiple removed resources ({} resources) in singleton Delta update",
          removed_resources.size()));
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
