#pragma once

#include <memory>
#include <string>
#include <vector>

#include "envoy/config/subscription.h"

#include "absl/status/status.h"
#include "absl/strings/string_view.h"

namespace Envoy {
namespace Config {

class SingletonSubscriptionCallbacksAdapter : public SubscriptionCallbacks {
public:
  SingletonSubscriptionCallbacksAdapter(SingletonSubscriptionCallbacks& callbacks);

  // SotW xDS Overload
  absl::Status onConfigUpdate(const std::vector<DecodedResourceRef>& resources,
                              const std::string& version_info) override;

  // Delta xDS Overload
  absl::Status onConfigUpdate(const std::vector<DecodedResourceRef>& added_resources,
                              const Protobuf::RepeatedPtrField<std::string>& removed_resources,
                              const std::string& system_version_info) override;

  void onConfigUpdateFailed(ConfigUpdateFailureReason reason, const EnvoyException* e) override;

private:
  SingletonSubscriptionCallbacks& callbacks_;
};

class SingletonSubscriptionImpl : public SingletonSubscription {
public:
  SingletonSubscriptionImpl(SubscriptionPtr sub, absl::string_view resource_name,
                            std::unique_ptr<SingletonSubscriptionCallbacksAdapter> adapter);

  void start() override;

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
