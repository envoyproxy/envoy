#include "extensions/common/aws/region_provider_impl.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Aws {

static const char AWS_REGION[] = "AWS_REGION";

StaticRegionProvider::StaticRegionProvider(const std::string& region) : region_(region) {}

absl::optional<std::string> StaticRegionProvider::getRegion() {
  return absl::optional<std::string>(region_);
}

absl::optional<std::string> EnvironmentRegionProvider::getRegion() {
  const auto region = std::getenv(AWS_REGION);
  if (region == nullptr) {
    return absl::nullopt;
  }
  ENVOY_LOG(debug, "Found environment region {}={}", AWS_REGION, region);
  return absl::optional<std::string>(region);
}

} // namespace Aws
} // namespace Common
} // namespace Extensions
} // namespace Envoy
