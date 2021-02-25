#include "common/version/api_version.h"

#include <string>

#include "common/common/fmt.h"

#include "absl/strings/numbers.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"

namespace Envoy {

std::string
ApiVersionInfo::apiVersionToString(const envoy::config::core::v3::ApiVersionNumber& version) {
  return fmt::format("{}.{}.{}", version.version().major_number(), version.version().minor_number(),
                     version.version().patch());
}

const envoy::config::core::v3::ApiVersionNumber& ApiVersionInfo::apiVersion() {
  static const auto* result =
      new envoy::config::core::v3::ApiVersionNumber(makeApiVersion(API_VERSION_NUMBER));
  return *result;
}

const envoy::config::core::v3::ApiVersionNumber& ApiVersionInfo::oldestApiVersion() {
  static const auto* result =
      new envoy::config::core::v3::ApiVersionNumber(computeOldestApiVersion(apiVersion()));
  return *result;
}

envoy::config::core::v3::ApiVersionNumber ApiVersionInfo::makeApiVersion(const char* version) {
  envoy::config::core::v3::ApiVersionNumber result;
  // Split API_VERSION_NUMBER into version
  const std::vector<std::string> ver_split = absl::StrSplit(version, '.');
  if (ver_split.size() == 3) {
    uint32_t major = 0;
    uint32_t minor = 0;
    uint32_t patch = 0;
    if (absl::SimpleAtoi(ver_split[0], &major) && absl::SimpleAtoi(ver_split[1], &minor) &&
        absl::SimpleAtoi(ver_split[2], &patch)) {
      result.mutable_version()->set_major_number(major);
      result.mutable_version()->set_minor_number(minor);
      result.mutable_version()->set_patch(patch);
    }
  }
  return result;
}

envoy::config::core::v3::ApiVersionNumber ApiVersionInfo::computeOldestApiVersion(
    const envoy::config::core::v3::ApiVersionNumber& latest_version) {
  envoy::config::core::v3::ApiVersionNumber result;
  // Envoy supports up to 2 most recent minor versions. Therefore if the latest
  // API version "X.Y.Z", Envoy's oldest API version is "X.Y-1.0".
  // Note that the major number is always the same as the latest version,
  // and the patch number is always 0. In addition, the minor number is at
  // least 0, and the oldest api version cannot be set to a previous major number.
  result.mutable_version()->set_major_number(latest_version.version().major_number());
  result.mutable_version()->set_minor_number(std::max(
      static_cast<int64_t>(latest_version.version().minor_number()) - 1, static_cast<int64_t>(0)));
  result.mutable_version()->set_patch(0);
  return result;
}
} // namespace Envoy
