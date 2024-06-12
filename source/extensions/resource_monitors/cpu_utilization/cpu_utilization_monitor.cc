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

#include "absl/strings/str_split.h"

namespace Envoy {
namespace Extensions {
namespace ResourceMonitors {
namespace CpuUtilizationMonitor {

CpuUtilizationMonitor::CpuUtilizationMonitor(
    const envoy::extensions::resource_monitors::cpu_utilization::v3::
        CpuUtilizationConfig& /*config*/,
    std::unique_ptr<CpuStatsReader> cpu_stats_reader)
    : utilization_(0.0), cpu_stats_reader_(std::move(cpu_stats_reader)) {
  previous_cpu_times_ = cpu_stats_reader_->getCpuTimes();
}

void CpuUtilizationMonitor::updateResourceUsage(Server::ResourceUpdateCallbacks& callbacks) {
  CpuTimes cpu_times = cpu_stats_reader_->getCpuTimes();
  if (!cpu_times.is_valid) {
    ENVOY_LOG_MISC(error, "Can't open file to read CPU utilization");
    const auto& error = EnvoyException("Can't open file to read CPU utilization");
    callbacks.onFailure(error);
    return;
  }

  const int64_t work_over_period = cpu_times.work_time - previous_cpu_times_.work_time;
  const int64_t total_over_period = cpu_times.total_time - previous_cpu_times_.total_time;
  if (work_over_period < 0 || total_over_period <= 0) {
    ENVOY_LOG_MISC(error,
                   "CPUStatsReader returned stats that are erroneous. work_over_period={} cannot "
                   "be a negative number and total_over_period={} must be a positive number.",
                   work_over_period, total_over_period);
    const auto& error = EnvoyException("Erroneous CPU stats calculation");
    callbacks.onFailure(error);
    return;
  }
  const double current_utilization = static_cast<double>(work_over_period) / total_over_period;
  ENVOY_LOG_MISC(trace, "Prev work={}, Cur work={}, Prev Total={}, Cur Total={}",
                 previous_cpu_times_.work_time, cpu_times.work_time, previous_cpu_times_.total_time,
                 cpu_times.total_time);
  // The new utilization is calculated/smoothed using EWMA
  // (https://en.wikipedia.org/wiki/EWMA_chart) where we choose a very small dampening alpha to give
  // past calculations higher priority.
  utilization_ = current_utilization * DAMPENING_ALPHA + (1 - DAMPENING_ALPHA) * utilization_;
  ENVOY_LOG_MISC(trace, "CPU Utilization={}", utilization_ * 100);

  Server::ResourceUsage usage;
  usage.resource_pressure_ = utilization_;

  callbacks.onSuccess(usage);

  previous_cpu_times_ = cpu_times;
}

} // namespace CpuUtilizationMonitor
} // namespace ResourceMonitors
} // namespace Extensions
} // namespace Envoy
