#pragma once

#include <string>

#include "envoy/extensions/resource_monitors/cpu_utilization/v3/cpu_utilization.pb.h"
#include "envoy/server/resource_monitor.h"

#include "source/extensions/resource_monitors/cpu_utilization/cpu_stats_reader.h"

namespace Envoy {
namespace Extensions {
namespace ResourceMonitors {
namespace CpuUtilizationMonitor {

static const std::string LINUX_CPU_STATS_FILE = "/proc/stat"; // CPU Stats for Host Machine
static const std::string LINUX_CGROUP_CPU_ALLOCATED_FILE =
    "/sys/fs/cgroup/cpu/cpu.shares"; // CGROUP Container Allocated Millicores
static const std::string LINUX_CGROUP_CPU_TIMES_FILE =
    "/sys/fs/cgroup/cpu/cpuacct.usage"; // CGROUP Container Usage Nanoseconds
static const std::string LINUX_UPTIME_FILE =
    "/proc/uptime"; // System boot UPTIME in seconds, precision is 0.01s

class LinuxCpuStatsReader : public CpuStatsReader {
public:
  LinuxCpuStatsReader(
      envoy::extensions::resource_monitors::cpu_utilization::v3::CpuUtilizationConfig::
          UtilizationComputeStrategy mode_ = envoy::extensions::resource_monitors::cpu_utilization::
              v3::CpuUtilizationConfig::HOST, // Default mode of calculation: HOST CPU USage
      const std::string& cpu_stats_filename = LINUX_CPU_STATS_FILE,
      const std::string& linux_cgroup_cpu_allocated_file = LINUX_CGROUP_CPU_ALLOCATED_FILE,
      const std::string& linux_cgroup_cpu_times_file = LINUX_CGROUP_CPU_TIMES_FILE,
      const std::string& linux_uptime_file = LINUX_UPTIME_FILE);
  CpuTimes getCpuTimes() override;

private:
  envoy::extensions::resource_monitors::cpu_utilization::v3::CpuUtilizationConfig::
      UtilizationComputeStrategy mode_;
  const std::string cpu_stats_filename_;
  const std::string linux_cgroup_cpu_allocated_file_;
  const std::string linux_cgroup_cpu_times_file_;
  const std::string linux_uptime_file_;
  CpuTimes getHostCpuTimes();
  CpuTimes getContainerCpuTimes();
};

} // namespace CpuUtilizationMonitor
} // namespace ResourceMonitors
} // namespace Extensions
} // namespace Envoy
