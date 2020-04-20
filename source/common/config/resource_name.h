#pragma once

#include <string>
#include <vector>

#include "envoy/config/core/v3/config_source.pb.h"

#include "common/common/assert.h"
#include "common/config/api_type_oracle.h"

namespace Envoy {
namespace Config {

/**
 * Get resource name from api type and version.
 */
template <typename Current>
std::string getResourceName(envoy::config::core::v3::ApiVersion resource_api_version) {
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

/**
 * Get type url from api type and version.
 */
template <typename Current>
std::string getTypeUrl(envoy::config::core::v3::ApiVersion resource_api_version) {
  return "type.googleapis.com/" + getResourceName<Current>(resource_api_version);
}

/**
 * get all version resource names.
 */
template <typename Current> std::vector<std::string> getAllVersionResourceNames() {
  return std::vector<std::string>{
      Current().GetDescriptor()->full_name(),
      ApiTypeOracle::getEarlierVersionMessageTypeName(Current().GetDescriptor()->full_name())
          .value()};
}

/**
 * get all version type urls.
 */
template <typename Current> std::vector<std::string> getAllVersionTypeUrls() {
  auto resource_names = getAllVersionResourceNames<Current>();
  for (auto&& resource_name : resource_names) {
    resource_name = "type.googleapis.com/" + resource_name;
  }
  return resource_names;
}

} // namespace Config
} // namespace Envoy
