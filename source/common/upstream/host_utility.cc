#include "common/upstream/host_utility.h"

#include <string>

namespace Envoy {
namespace Upstream {

std::string HostUtility::healthFlagsToString(const Host& host) {
  if (host.healthy()) {
    return "healthy";
  }

  std::string ret;
  if (host.healthFlagGet(Host::HealthFlag::FAILED_ACTIVE_HC)) {
    ret += "/failed_active_hc";
  }

  if (host.healthFlagGet(Host::HealthFlag::FAILED_OUTLIER_CHECK)) {
    ret += "/failed_outlier_check";
  }

  return ret;
}

} // namespace Upstream
} // namespace Envoy
