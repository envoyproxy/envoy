#pragma once

#include <string>

#include "source/extensions/resource_monitors/cpu_utilization/cpu_stats_reader.h"

namespace Envoy {
namespace Extensions {
namespace ResourceMonitors {
namespace CpuUtilizationMonitor {

static const std::string LINUX_CPU_STATS_FILE = "/proc/stat";
static const std::string LINUX_CGROUP_CPU_ALLOCATED_FILE = "/sys/fs/cgroup/cpu/cpu.shares";
static const std::string LINUX_CGROUP_CPU_TIMES_FILE = "/sys/fs/cgroup/cpu/cpuacct.usage";
static const std::string LINUX_UPTIME_FILE = "/proc/uptime";

class LinuxCpuStatsReader : public CpuStatsReader {
public:
  LinuxCpuStatsReader(const std::string& cpu_stats_filename = LINUX_CPU_STATS_FILE);
  CpuTimes getCpuTimes() override;

private:
  const std::string cpu_stats_filename_;
};

class LinuxContainerCpuStatsReader : public CgroupStatsReader {
public:
  LinuxContainerCpuStatsReader(
      const std::string& linux_cgroup_cpu_allocated_file = LINUX_CGROUP_CPU_ALLOCATED_FILE,
      const std::string& linux_cgroup_cpu_times_file = LINUX_CGROUP_CPU_TIMES_FILE,
      const std::string& linux_uptime_file = LINUX_UPTIME_FILE);
  CpuTimes getCgroupStats() override;

private:
  const std::string linux_cgroup_cpu_allocated_file_;
  const std::string linux_cgroup_cpu_times_file_;
  const std::string linux_uptime_file_; // Uptime in seconds
};

} // namespace CpuUtilizationMonitor
} // namespace ResourceMonitors
} // namespace Extensions
} // namespace Envoy
