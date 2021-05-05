#include "common/upstream/host_utility.h"

#include <string>

namespace Envoy {
namespace Upstream {
namespace {

void setHealthFlag(Upstream::Host::HealthFlag flag, const Host& host, std::string& health_status) {
  switch (flag) {
  case Host::HealthFlag::FAILED_ACTIVE_HC: {
    if (host.healthFlagGet(Host::HealthFlag::FAILED_ACTIVE_HC)) {
      health_status += "/failed_active_hc";
    }
    break;
  }

  case Host::HealthFlag::FAILED_OUTLIER_CHECK: {
    if (host.healthFlagGet(Host::HealthFlag::FAILED_OUTLIER_CHECK)) {
      health_status += "/failed_outlier_check";
    }
    break;
  }

  case Host::HealthFlag::FAILED_EDS_HEALTH: {
    if (host.healthFlagGet(Host::HealthFlag::FAILED_EDS_HEALTH)) {
      health_status += "/failed_eds_health";
    }
    break;
  }

  case Host::HealthFlag::DEGRADED_ACTIVE_HC: {
    if (host.healthFlagGet(Host::HealthFlag::DEGRADED_ACTIVE_HC)) {
      health_status += "/degraded_active_hc";
    }
    break;
  }

  case Host::HealthFlag::DEGRADED_EDS_HEALTH: {
    if (host.healthFlagGet(Host::HealthFlag::DEGRADED_EDS_HEALTH)) {
      health_status += "/degraded_eds_health";
    }
    break;
  }

  case Host::HealthFlag::PENDING_DYNAMIC_REMOVAL: {
    if (host.healthFlagGet(Host::HealthFlag::PENDING_DYNAMIC_REMOVAL)) {
      health_status += "/pending_dynamic_removal";
    }
    break;
  }

  case Host::HealthFlag::PENDING_ACTIVE_HC: {
    if (host.healthFlagGet(Host::HealthFlag::PENDING_ACTIVE_HC)) {
      health_status += "/pending_active_hc";
    }
    break;
  }

  case Host::HealthFlag::EXCLUDED_VIA_IMMEDIATE_HC_FAIL: {
    if (host.healthFlagGet(Host::HealthFlag::EXCLUDED_VIA_IMMEDIATE_HC_FAIL)) {
      health_status += "/excluded_via_immediate_hc_fail";
    }
    break;
  }

  case Host::HealthFlag::ACTIVE_HC_TIMEOUT: {
    if (host.healthFlagGet(Host::HealthFlag::ACTIVE_HC_TIMEOUT)) {
      health_status += "/active_hc_timeout";
    }
    break;
  }
  }
}

} // namespace

std::string HostUtility::healthFlagsToString(const Host& host) {
  std::string health_status;

  // Invokes setHealthFlag for each health flag.
#define SET_HEALTH_FLAG(name, notused)                                                             \
  setHealthFlag(Upstream::Host::HealthFlag::name, host, health_status);
  HEALTH_FLAG_ENUM_VALUES(SET_HEALTH_FLAG)
#undef SET_HEALTH_FLAG

  if (health_status.empty()) {
    return "healthy";
  } else {
    return health_status;
  }
}

} // namespace Upstream
} // namespace Envoy
