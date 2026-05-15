#pragma once

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "envoy/config/subscription.h"

#include "source/common/common/assert.h"
#include "source/common/common/fmt.h"

#include "absl/status/status.h"
#include "absl/strings/string_view.h"

namespace Envoy {
namespace Config {

class SingletonSubscriptionCallbacksAdapter : public SubscriptionCallbacks {
public:
  SingletonSubscriptionCallbacksAdapter(SingletonSubscriptionCallbacks& callbacks)
      : callbacks_(callbacks) {}

  // SotW xDS Overload
  absl::Status onConfigUpdate(const std::vector<DecodedResourceRef>& resources,
                              const std::string& version_info) override {
    if (resources.empty()) {
      callbacks_.onResourceRemoved();
      return absl::OkStatus();
    }
    if (resources.size() > 1) {
      return absl::InvalidArgumentError(
          fmt::format("Unexpected multiple resources ({} resources) in singleton SotW update",
                      resources.size()));
    }
    return callbacks_.onResourceUpdate(resources[0].get(), version_info);
  }

  // Delta xDS Overload
  absl::Status onConfigUpdate(const std::vector<DecodedResourceRef>& added_resources,
                              const Protobuf::RepeatedPtrField<std::string>& removed_resources,
                              const std::string& system_version_info) override {
    if (!added_resources.empty()) {
      if (added_resources.size() > 1) {
        return absl::InvalidArgumentError(
            fmt::format("Unexpected multiple added resources ({} resources) in singleton Delta update",
                        added_resources.size()));
      }
      return callbacks_.onResourceUpdate(added_resources[0].get(), system_version_info);
    }
    if (!removed_resources.empty()) {
      if (removed_resources.size() > 1) {
        return absl::InvalidArgumentError(
            fmt::format("Unexpected multiple removed resources ({} resources) in singleton Delta update",
                        removed_resources.size()));
      }
      callbacks_.onResourceRemoved();
      return absl::OkStatus();
    }
    // Both added_resources and removed_resources are empty (heartbeat / no-op update).
    return absl::OkStatus();
  }

  void onConfigUpdateFailed(ConfigUpdateFailureReason reason, const EnvoyException* e) override {
    callbacks_.onFailure(reason, e);
  }

private:
  SingletonSubscriptionCallbacks& callbacks_;
};

class SingletonSubscriptionImpl : public SingletonSubscription {
public:
  SingletonSubscriptionImpl(SubscriptionPtr sub, absl::string_view resource_name,
                            std::unique_ptr<SingletonSubscriptionCallbacksAdapter> adapter)
      : adapter_(std::move(adapter)), sub_(std::move(sub)), resource_name_(resource_name) {}

  void start() override { sub_->start({resource_name_}); }

private:
  // adapter_ must outlive sub_ because sub_ holds a reference to *adapter_.
  // In C++, members are destroyed in reverse order of declaration.
  // Therefore, adapter_ must be declared before sub_.
  std::unique_ptr<SingletonSubscriptionCallbacksAdapter> adapter_;
  SubscriptionPtr sub_;
  std::string resource_name_;
};

} // namespace Config
} // namespace Envoy
