#include "common/aws/region_provider_impl.h"

namespace Envoy {
namespace Aws {
namespace Auth {

static const char AWS_REGION[] = "AWS_REGION";

absl::optional<std::string> EnvironmentRegionProvider::getRegion() {
  const auto region = std::getenv(AWS_REGION);
  if (region == nullptr) {
    return absl::optional<std::string>();
  }
  ENVOY_LOG(debug, "Found environment region {}={}", AWS_REGION, region);
  return absl::optional<std::string>(region);
}

} // namespace Auth
} // namespace Aws
} // namespace Envoy