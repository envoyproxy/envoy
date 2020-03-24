#pragma once

#include <string>

#include "envoy/config/discovery_service_base.h"
#include "envoy/config/subscription.h"

#include "common/config/api_type_oracle.h"

namespace Envoy {
namespace Config {
template <typename Current> struct SubscriptionBase : public Config::SubscriptionCallbacks {
  static std::string getResourceName(envoy::config::core::v3::ApiVersion resource_api_version) {
    switch (resource_api_version) {
    case envoy::config::core::v3::ApiVersion::AUTO:
    case envoy::config::core::v3::ApiVersion::V2:
      return ApiTypeOracle::getEarlierVersionMessageTypeName(Current().GetDescriptor()->full_name())
          .value();
    case envoy::config::core::v3::ApiVersion::V3:
      return Current().GetDescriptor()->full_name();
    default:
      NOT_REACHED_GCOVR_EXCL_LINE;
    }
  }
};

} // namespace Config
} // namespace Envoy