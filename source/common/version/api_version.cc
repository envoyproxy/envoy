#include "common/version/api_version.h"

#include <string>

#include "common/common/fmt.h"

#include "absl/strings/numbers.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"

namespace Envoy {

const ApiVersion& ApiVersionInfo::apiVersion() { return api_version; }

const ApiVersion& ApiVersionInfo::oldestApiVersion() {
  static const auto* result = new ApiVersion(computeOldestApiVersion(apiVersion()));
  return *result;
}

ApiVersion ApiVersionInfo::computeOldestApiVersion(const ApiVersion& latest_version) {
  // Envoy supports up to 2 most recent minor versions. Therefore if the latest
  // API version "X.Y.Z", Envoy's oldest API version is "X.Y-1.0".
  // Note that the major number is always the same as the latest version,
  // and the patch number is always 0. In addition, the minor number is at
  // least 0, and the oldest api version cannot be set to a previous major number.
  return {latest_version.major,
          static_cast<uint32_t>(
              std::max(static_cast<int64_t>(latest_version.minor) - 1, static_cast<int64_t>(0))),
          0};
}

} // namespace Envoy
