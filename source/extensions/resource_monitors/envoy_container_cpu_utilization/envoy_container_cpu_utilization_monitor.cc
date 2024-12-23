#include "source/extensions/resource_monitors/envoy_container_cpu_utilization/envoy_container_cpu_utilization_monitor.h"

#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>

#include "source/common/common/fmt.h"

#include "absl/strings/str_split.h"

namespace Envoy {
namespace Extensions {
namespace ResourceMonitors {
namespace EnvoyContainerCpuUtilizationMonitor {

// The dampening alpha value used for EWMA calculation.
// The value is chosen to be very small to give past calculations higher priority.
// This helps in reducing the impact of sudden spikes or drops in CPU utilization.
constexpr double DAMPENING_ALPHA = 0.05;

EnvoyContainerCpuUtilizationMonitor::EnvoyContainerCpuUtilizationMonitor(
    const envoy::extensions::resource_monitors::envoy_container_cpu_utilization::v3::
        EnvoyContainerCpuUtilizationConfig& /*config*/,
    std::unique_ptr<ContainerStatsReader> envoy_container_stats_reader)
    : envoy_container_stats_reader_(std::move(envoy_container_stats_reader)) {
  previous_envoy_container_stats_ = envoy_container_stats_reader_->getEnvoyContainerStats();
}

void EnvoyContainerCpuUtilizationMonitor::updateResourceUsage(Server::ResourceUpdateCallbacks& callbacks) {
  EnvoyContainerStats envoy_container_stats = envoy_container_stats_reader_->getEnvoyContainerStats();
  if (!envoy_container_stats.is_valid) {
    const auto& error = EnvoyException("Can't open Cgroup cpu stat files");
    callbacks.onFailure(error);
    return;
  }
  int64_t cpu_milli_cores = envoy_container_stats.cpu_allocated_millicores_;
  if (cpu_milli_cores <= 0){
    const auto &error = EnvoyException(fmt::format("Erroneous CPU Allocated Value: '{}', should be a positive number",cpu_milli_cores));
    callbacks.onFailure(error);
    return;
  }

  int64_t cpu_work = envoy_container_stats.total_cpu_times_ns_ - previous_envoy_container_stats_.total_cpu_times_ns_;
  if (cpu_work <= 0){
    const auto& error = EnvoyException(fmt::format("Erroneous CPU Work Value: '{}', should be a positive number",cpu_work));
    callbacks.onFailure(error);
    return;
  }

  double system_time_elapsed_milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>( envoy_container_stats.system_time_ - previous_envoy_container_stats_.system_time_ ).count();
  if (system_time_elapsed_milliseconds <= 0){
    const auto& error = EnvoyException(fmt::format("Erroneous Value of Elapsed Time: '{}', should be a positive number",system_time_elapsed_milliseconds));
    callbacks.onFailure(error);
    return;
  }


  double cpu_usage = (system_time_elapsed_milliseconds > 0 && cpu_milli_cores > 0 && cpu_work > 0 ) ? cpu_work / (system_time_elapsed_milliseconds * 1000 * cpu_milli_cores) : 0;
  ENVOY_LOG_MISC(trace, "Current CPU work={}, Current CPU USAGE Time Delta work={}", cpu_work, system_time_elapsed_milliseconds);
  
  // The new utilization is calculated/smoothed using EWMA
  utilization_ = cpu_usage * DAMPENING_ALPHA + (1 - DAMPENING_ALPHA) * utilization_;

  Server::ResourceUsage usage;
  usage.resource_pressure_ = utilization_;

  callbacks.onSuccess(usage);
  previous_envoy_container_stats_ = envoy_container_stats;
}

} // namespace EnvoyContainerCpuUtilizationMonitor
} // namespace ResourceMonitors
} // namespace Extensions
} // namespace Envoy
