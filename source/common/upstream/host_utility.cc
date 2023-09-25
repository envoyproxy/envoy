#include "source/common/upstream/host_utility.h"

#include <string>

#include "source/common/config/well_known_names.h"
#include "source/common/runtime/runtime_features.h"

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

  if (!Runtime::runtimeFeatureEnabled(
          "envoy.reloadable_features.validate_detailed_override_host_statuses")) {
    // Old code path that should be removed once the runtime flag is removed directly.
    // Coarse health status is used here.

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

  if (!common_config.has_override_host_status()) {
    // No override host status and [UNKNOWN, HEALTHY, DEGRADED] will be applied by default.
    override_host_status.set(static_cast<uint32_t>(envoy::config::core::v3::HealthStatus::UNKNOWN));
    override_host_status.set(static_cast<uint32_t>(envoy::config::core::v3::HealthStatus::HEALTHY));
    override_host_status.set(
        static_cast<uint32_t>(envoy::config::core::v3::HealthStatus::DEGRADED));
    return override_host_status;
  }

  for (auto single_status : common_config.override_host_status().statuses()) {
    switch (static_cast<envoy::config::core::v3::HealthStatus>(single_status)) {
      PANIC_ON_PROTO_ENUM_SENTINEL_VALUES;
    case envoy::config::core::v3::HealthStatus::UNKNOWN:
    case envoy::config::core::v3::HealthStatus::HEALTHY:
    case envoy::config::core::v3::HealthStatus::UNHEALTHY:
    case envoy::config::core::v3::HealthStatus::DRAINING:
    case envoy::config::core::v3::HealthStatus::TIMEOUT:
    case envoy::config::core::v3::HealthStatus::DEGRADED:
      override_host_status.set(static_cast<uint32_t>(single_status));
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

  if (!Runtime::runtimeFeatureEnabled(
          "envoy.reloadable_features.validate_detailed_override_host_statuses")) {
    // Old code path that should be removed once the runtime flag is removed directly.
    // Coarse health status is used here.

    if (status[static_cast<size_t>(host->coarseHealth())]) {
      return host;
    }
    return nullptr;
  }

  if (status[static_cast<uint32_t>(host->healthStatus())]) {
    return host;
  }
  return nullptr;
}

namespace {
template <class StatType, typename GetStatsFunc>
void forEachMetric(const ClusterManager& cluster_manager,
                   const std::function<void(StatType&& metric)>& cb,
                   GetStatsFunc get_stats_vector) {
  for (const auto& [cluster_name, cluster_ref] : cluster_manager.clusters().active_clusters_) {
    if (cluster_ref.get().info()->perEndpointStats()) {
      for (auto& host_set : cluster_ref.get().prioritySet().hostSetsPerPriority()) {
        for (auto& host : host_set->hosts()) {
          Stats::TagVector tags = {{Envoy::Config::TagNames::get().CLUSTER_NAME, cluster_name},
                                   {"envoy.endpoint_address", host->address()->asString()}};
          const auto& hostname = host->hostname();
          if (!hostname.empty()) {
            tags.push_back({"envoy.endpoint_hostname", hostname});
          }

          for (auto& [metric_name, primitive] : get_stats_vector(*host)) {
            StatType metric(primitive.get());

            metric.setName(absl::StrCat("cluster.", cluster_name, ".endpoint.", metric_name, ".",
                                        host->address()->asStringView()));
            metric.setTagExtractedName(absl::StrCat("cluster.endpoint.", metric_name));
            metric.setTags(tags);

            cb(std::move(metric));
          }
        }
      }
    }
  }
}
} // namespace

void HostUtility::forEachHostCounter(
    const ClusterManager& cluster_manager,
    const std::function<void(Stats::PrimitiveCounterSnapshot&& metric)>& cb) {
  forEachMetric(cluster_manager, cb, [](Host& host) { return host.counters(); });
}

void HostUtility::forEachHostGauge(
    const ClusterManager& cluster_manager,
    const std::function<void(Stats::PrimitiveGaugeSnapshot&& metric)>& cb) {
  // This is held as a reference in the returned vector of gauges. It lives here so that it's
  // lifetime is longer than `forEachMetric`.
  Stats::PrimitiveGauge healthy_gauge;

  forEachMetric(cluster_manager, cb, [&](Host& host) {
    // std::pair<absl::string_view, Stats::PrimitiveGaugeReference>
    auto gauges = host.gauges();

    // Add synthentic "healthy" gauge.
    healthy_gauge.set((host.coarseHealth() == Host::Health::Healthy) ? 1 : 0);
    gauges.emplace_back(absl::string_view("healthy"), healthy_gauge);
    return gauges;
  });
}

} // namespace Upstream
} // namespace Envoy
