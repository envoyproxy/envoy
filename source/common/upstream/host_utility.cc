#include "source/common/upstream/host_utility.h"

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

HostUtility::HostStatusSet HostUtility::createOverrideHostStatus(
    const envoy::config::cluster::v3::Cluster::CommonLbConfig& common_config) {
  HostStatusSet override_host_status;

  if (!common_config.has_override_host_status()) {
    // No override host status and 'Healthy' and 'Degraded' will be applied by default.
    override_host_status.set(static_cast<size_t>(Host::Health::Healthy));
    override_host_status.set(static_cast<size_t>(Host::Health::Degraded));
    return override_host_status;
  }

  for (auto single_status : common_config.override_host_status().statuses()) {
    switch (static_cast<envoy::config::core::v3::HealthStatus>(single_status)) {
      PANIC_ON_PROTO_ENUM_SENTINEL_VALUES;
    case envoy::config::core::v3::HealthStatus::UNKNOWN:
    case envoy::config::core::v3::HealthStatus::HEALTHY:
      override_host_status.set(static_cast<size_t>(Host::Health::Healthy));
      break;
    case envoy::config::core::v3::HealthStatus::UNHEALTHY:
    case envoy::config::core::v3::HealthStatus::DRAINING:
    case envoy::config::core::v3::HealthStatus::TIMEOUT:
      override_host_status.set(static_cast<size_t>(Host::Health::Unhealthy));
      break;
    case envoy::config::core::v3::HealthStatus::DEGRADED:
      override_host_status.set(static_cast<size_t>(Host::Health::Degraded));
      break;
    }
  }
  return override_host_status;
}

HostConstSharedPtr HostUtility::selectOverrideHost(const HostMap* host_map, HostStatusSet status,
                                                   LoadBalancerContext* context) {
  if (context == nullptr) {
    return nullptr;
  }

  auto override_host = context->overrideHostToSelect();
  if (!override_host.has_value()) {
    return nullptr;
  }

  if (host_map == nullptr) {
    return nullptr;
  }

  auto host_iter = host_map->find(override_host.value());

  // The override host cannot be found in the host map.
  if (host_iter == host_map->end()) {
    return nullptr;
  }

  HostConstSharedPtr host = host_iter->second;
  ASSERT(host != nullptr);

  if (status[static_cast<size_t>(host->coarseHealth())]) {
    return host;
  }
  return nullptr;
}

} // namespace Upstream
} // namespace Envoy
