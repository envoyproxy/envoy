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
  previous_cpu_times_ = cpu_stats_reader_->getCpuTimes();
}

void CpuUtilizationMonitor::updateResourceUsage(Server::ResourceUpdateCallbacks& callbacks) {
  CpuTimes cpu_times = cpu_stats_reader_->getCpuTimes();
  if (!cpu_times.is_valid) {
    const auto& error = EnvoyException("Can't open file to read CPU utilization");
    callbacks.onFailure(error);
    return;
  }

  // Calculate utilization (encapsulates cgroup v1 vs v2 differences and validation)
  double current_utilization;
  absl::Status status = cpu_times.calculateUtilization(previous_cpu_times_, current_utilization);
  if (!status.ok()) {
    const auto& error = EnvoyException(std::string(status.message()));
    callbacks.onFailure(error);
    return;
  }

  // Debug logging
  ENVOY_LOG_MISC(
      trace, "CPU utilization: {}, work_time={}, total_time={}, effective_cores={} (cgroup v{})",
      current_utilization, cpu_times.work_time, cpu_times.total_time, cpu_times.effective_cores,
      cpu_times.is_cgroup_v2 ? 2 : 1);

  // The new utilization is calculated/smoothed using EWMA
  utilization_ = current_utilization * DAMPENING_ALPHA + (1 - DAMPENING_ALPHA) * utilization_;

  Server::ResourceUsage usage;
  usage.resource_pressure_ = utilization_;

  callbacks.onSuccess(usage);

  previous_cpu_times_ = cpu_times;
}

} // namespace CpuUtilizationMonitor
} // namespace ResourceMonitors
} // namespace Extensions
} // namespace Envoy
