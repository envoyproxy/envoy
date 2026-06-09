#pragma once

#include <string>

#include "envoy/stats/primitive_stats.h"
#include "envoy/upstream/load_balancer.h"
#include "envoy/upstream/upstream.h"

namespace Envoy {
namespace Upstream {

using CommonLbConfigProto = envoy::config::cluster::v3::Cluster::CommonLbConfig;

/**
 * Utility functions for hosts.
 */
class HostUtility {
public:
  using HostStatusSet = std::bitset<32>;

  /**
   * Convert a host's health flags into a debug string.
   */
  static std::string healthFlagsToString(const Host& host);

  // A utility function to create override host status from lb config.
  static HostStatusSet createOverrideHostStatus(const CommonLbConfigProto& common_config);

  // Status of override host selection.
  enum class OverrideHostSelectionStatus {
    // Host was successfully selected.
    Success,
    // Host was not found in the host map.
    NotFound,
    // Host was found but is not healthy.
    Unhealthy,
  };

  // Result of attempting to select an override host from the host map.
  struct OverrideHostSelectionResult {
    // The selected host, or nullptr if selection failed.
    HostConstSharedPtr host;
    // Whether strict mode was requested for the override.
    bool strict{false};
    // The status of the override host selection.
    OverrideHostSelectionStatus status{OverrideHostSelectionStatus::Success};
  };

  /**
   * A utility function to select override host from host map according to load balancer context.
   *
   * @return OverrideHostSelectionResult containing the selected host, whether strict mode was
   *         requested, and the reason for the selection outcome.
   */
  static OverrideHostSelectionResult
  selectOverrideHost(const HostMap* host_map, HostStatusSet status, LoadBalancerContext* context);

  // Iterate over all per-endpoint metrics, for clusters with `per_endpoint_stats` enabled.
  static void
  forEachHostMetric(const ClusterManager& cluster_manager,
                    const std::function<void(Stats::PrimitiveCounterSnapshot&& metric)>& counter_cb,
                    const std::function<void(Stats::PrimitiveGaugeSnapshot&& metric)>& gauge_cb);
};

} // namespace Upstream
} // namespace Envoy
