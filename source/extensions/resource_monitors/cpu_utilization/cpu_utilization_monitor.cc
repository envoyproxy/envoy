#include "source/extensions/resource_monitors/cpu_utilization/cpu_utilization_monitor.h"

#include <dirent.h>
#include <unistd.h>

#include <array>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>

#include "envoy/common/exception.h"

#include "source/common/common/assert.h"
#include "source/common/common/fmt.h"
#include "source/common/common/thread.h"

#include "absl/strings/str_split.h"

namespace Envoy {
namespace Extensions {
namespace ResourceMonitors {
namespace CpuUtilizationMonitor {

// The dampening alpha value used for EWMA calculation.
// The value is chosen to be very small to give past calculations higher priority.
// This helps in reducing the impact of sudden spikes or drops in CPU utilization.
constexpr double DAMPENING_ALPHA = 0.05;

CpuUtilizationMonitor::CpuUtilizationMonitor(
    const envoy::extensions::resource_monitors::cpu_utilization::v3::
        CpuUtilizationConfig& /*config*/,
    std::unique_ptr<CpuStatsReader> cpu_stats_reader)
    : cpu_stats_reader_(std::move(cpu_stats_reader)) {
  // Initialize by calling getUtilization() once to establish baseline
  (void)cpu_stats_reader_->getUtilization();
}

void CpuUtilizationMonitor::updateResourceUsage(Server::ResourceUpdateCallbacks& callbacks) {
  absl::StatusOr<double> utilization_result = cpu_stats_reader_->getUtilization();

  if (!utilization_result.ok()) {
    const auto& error = EnvoyException(std::string(utilization_result.status().message()));
    callbacks.onFailure(error);
    return;
  }

  const double current_utilization = utilization_result.value();

  // Debug logging
  ENVOY_LOG_MISC(trace, "CPU utilization: {}", current_utilization);

  // The new utilization is calculated/smoothed using EWMA
  utilization_ = current_utilization * DAMPENING_ALPHA + (1 - DAMPENING_ALPHA) * utilization_;

  Server::ResourceUsage usage;
  usage.resource_pressure_ = utilization_;

  callbacks.onSuccess(usage);
}

} // namespace CpuUtilizationMonitor
} // namespace ResourceMonitors
} // namespace Extensions
} // namespace Envoy
