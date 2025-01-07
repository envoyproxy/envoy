#pragma once

#include <chrono>
#include "envoy/common/time.h"
#include "envoy/extensions/resource_monitors/cpu_utilization/v3/cpu_utilization.pb.h"
#include "envoy/server/resource_monitor.h"

#include "source/common/runtime/runtime_features.h"
#include "source/extensions/resource_monitors/cpu_utilization/cpu_stats_reader.h"

namespace Envoy {
namespace Extensions {
namespace ResourceMonitors {
namespace CpuUtilizationMonitor {

class CpuUtilizationMonitor : public Server::ResourceMonitor {
public:
  CpuUtilizationMonitor(
      const envoy::extensions::resource_monitors::cpu_utilization::v3::CpuUtilizationConfig& config,
      std::unique_ptr<CpuStatsReader> cpu_stats_reader, TimeSource& time_source);
  
  CpuUtilizationMonitor(
    const envoy::extensions::resource_monitors::cpu_utilization::v3::CpuUtilizationConfig& config,
    std::unique_ptr<CgroupStatsReader> cgroup_stats_reader, TimeSource& time_source);

  void updateResourceUsage(Server::ResourceUpdateCallbacks& callbacks) override;
  void computeHostCpuUsage(Server::ResourceUpdateCallbacks& callbacks);
  void computeContainerCpuUsage(Server::ResourceUpdateCallbacks& callbacks);

private:
  double utilization_ = 0.0;
  CpuTimes previous_cpu_times_;
  CgroupStats previous_cgroup_stats_;
  std::unique_ptr<CpuStatsReader> cpu_stats_reader_;
  std::unique_ptr <CgroupStatsReader> cgroup_stats_reader_;
  TimeSource& time_source_;
  MonotonicTime last_update_time_;
  int16_t mode_ = -1; // Will be updated in Resource Monitor Class Constructor
};

} // namespace CpuUtilizationMonitor
} // namespace ResourceMonitors
} // namespace Extensions
} // namespace Envoy
