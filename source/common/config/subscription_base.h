#pragma once

#include "envoy/config/subscription.h"

#include "common/config/resource_name.h"

namespace Envoy {
namespace Config {

template <typename Current> struct SubscriptionBase : public Config::SubscriptionCallbacks {
public:
  SubscriptionBase(const envoy::config::core::v3::ApiVersion api_version)
      : api_version_(api_version) {}

  std::string getResourceName() const {
    return Envoy::Config::getResourceName<Current>(api_version_);
  }

private:
  const envoy::config::core::v3::ApiVersion api_version_;
};

} // namespace Config
} // namespace Envoy