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

#include "source/common/common/fmt.h"

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
        CpuUtilizationConfig& config,
    std::unique_ptr<CpuStatsReader> cpu_stats_reader,TimeSource &time_source)
    : cpu_stats_reader_(std::move(cpu_stats_reader)),time_source_(time_source),last_update_time_(time_source.monotonicTime()),mode_(config.mode()) {
  previous_cpu_times_ = cpu_stats_reader_->getCpuTimes();
}

CpuUtilizationMonitor::CpuUtilizationMonitor(
  const envoy::extensions::resource_monitors::cpu_utilization::v3::
        CpuUtilizationConfig& config,
  std::unique_ptr<CgroupStatsReader> cgroup_stats_reader,TimeSource &time_source)
  : cgroup_stats_reader_(std::move(cgroup_stats_reader)),time_source_(time_source),last_update_time_(time_source.monotonicTime()),mode_(config.mode()) {
    previous_cgroup_stats_ = cgroup_stats_reader_->getCgroupStats();
  } 

void CpuUtilizationMonitor::updateResourceUsage(Server::ResourceUpdateCallbacks& callbacks) {
  switch (mode_)
  {
  case envoy::extensions::resource_monitors::cpu_utilization::v3::UtilizationComputeStrategy::HOST:
    computeHostCpuUsage(callbacks);
    break;
  
  case envoy::extensions::resource_monitors::cpu_utilization::v3::UtilizationComputeStrategy::CONTAINER:
    computeContainerCpuUsage(callbacks);
    break;

  default:
    break;
  }
}

void CpuUtilizationMonitor::computeHostCpuUsage(Server::ResourceUpdateCallbacks& callbacks) {
  CpuTimes cpu_times = cpu_stats_reader_->getCpuTimes();
  if (!cpu_times.is_valid) {
    const auto& error = EnvoyException("Can't open file to read CPU utilization");
    callbacks.onFailure(error);
    return;
  }

  const int64_t work_over_period = cpu_times.work_time - previous_cpu_times_.work_time;
  const int64_t total_over_period = cpu_times.total_time - previous_cpu_times_.total_time;
  if (work_over_period < 0 || total_over_period <= 0) {
    const auto& error = EnvoyException(
        fmt::format("Erroneous CPU stats calculation. Work_over_period='{}' cannot "
                    "be a negative number and total_over_period='{}' must be a positive number.",
                    work_over_period, total_over_period));
    callbacks.onFailure(error);
    return;
  }
  const double current_utilization = static_cast<double>(work_over_period) / total_over_period;
  ENVOY_LOG_MISC(trace, "Prev work={}, Cur work={}, Prev Total={}, Cur Total={}",
                 previous_cpu_times_.work_time, cpu_times.work_time, previous_cpu_times_.total_time,
                 cpu_times.total_time);
  // The new utilization is calculated/smoothed using EWMA
  utilization_ = current_utilization * DAMPENING_ALPHA + (1 - DAMPENING_ALPHA) * utilization_;

  Server::ResourceUsage usage;
  usage.resource_pressure_ = utilization_;

  callbacks.onSuccess(usage);

  previous_cpu_times_ = cpu_times;
}

void CpuUtilizationMonitor::computeContainerCpuUsage(Server::ResourceUpdateCallbacks& callbacks) {
  CgroupStats envoy_container_stats = cgroup_stats_reader_->getCgroupStats();
  if (!envoy_container_stats.is_valid) {
    const auto& error = EnvoyException("Can't open Cgroup cpu stat files");
    callbacks.onFailure(error);
    return;
  }
  uint64_t cpu_milli_cores = envoy_container_stats.cpu_allocated_millicores_;
  if (cpu_milli_cores <= 0){
    const auto &error = EnvoyException(fmt::format("Erroneous CPU Allocated Value: '{}', should be a positive number",cpu_milli_cores));
    callbacks.onFailure(error);
    return;
  }

  uint64_t cpu_work = envoy_container_stats.total_cpu_times_ns_ - previous_cgroup_stats_.total_cpu_times_ns_;
  if (cpu_work <= 0){
    const auto& error = EnvoyException(fmt::format("Erroneous CPU Work Value: '{}', should be a positive number",cpu_work));
    callbacks.onFailure(error);
    return;
  }

  MonotonicTime current_time = time_source_.monotonicTime();

  double system_time_elapsed_milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(current_time - last_update_time_ ).count();
  if (system_time_elapsed_milliseconds <= 0){
    const auto& error = EnvoyException(fmt::format("Erroneous Value of Elapsed Time: '{}', should be a positive number",system_time_elapsed_milliseconds));
    callbacks.onFailure(error);
    return;
  }

  last_update_time_ = current_time;
  double cpu_usage = (system_time_elapsed_milliseconds > 0 && cpu_milli_cores > 0 && cpu_work > 0 ) ? cpu_work / (system_time_elapsed_milliseconds * 1000 * cpu_milli_cores) : 0;
  // The new utilization is calculated/smoothed using EWMA
  utilization_ = cpu_usage * DAMPENING_ALPHA + (1 - DAMPENING_ALPHA) * utilization_;

  Server::ResourceUsage usage;
  usage.resource_pressure_ = utilization_;

  callbacks.onSuccess(usage);
  previous_cgroup_stats_ = envoy_container_stats;
}

} // namespace CpuUtilizationMonitor
} // namespace ResourceMonitors
} // namespace Extensions
} // namespace Envoy
