#pragma once

#include <array>
#include <fstream>
#include <sstream>
#include <string>

#include "envoy/common/time.h"

#include "source/common/common/logger.h"
#include "source/extensions/resource_monitors/cpu_utilization/cpu_stats_reader.h"

namespace Envoy {
namespace Extensions {
namespace ResourceMonitors {
namespace CpuUtilizationMonitor {

static const std::string LINUX_CPU_STATS_FILE = "/proc/stat";
static const std::string LINUX_CGROUP_CPU_ALLOCATED_FILE = "/sys/fs/cgroup/cpu/cpu.shares";
static const std::string LINUX_CGROUP_CPU_TIMES_FILE = "/sys/fs/cgroup/cpu/cpuacct.usage";
// cgroup v2 unified hierarchy files
static const std::string LINUX_CGROUPV2_CPU_STAT_FILE = "/sys/fs/cgroup/cpu.stat";
static const std::string LINUX_CGROUPV2_CPU_MAX_FILE = "/sys/fs/cgroup/cpu.max";
static const std::string LINUX_CGROUPV2_CPU_EFFECTIVE_FILE = "/sys/fs/cgroup/cpuset.cpus.effective";

class LinuxCpuStatsReader : public CpuStatsReader {
public:
  LinuxCpuStatsReader(const std::string& cpu_stats_filename = LINUX_CPU_STATS_FILE);
  CpuTimes getCpuTimes() override;

private:
  const std::string cpu_stats_filename_;
};

class LinuxContainerCpuStatsReader : public CpuStatsReader {
public:
  LinuxContainerCpuStatsReader(
      TimeSource& time_source,
      const std::string& linux_cgroup_cpu_allocated_file = LINUX_CGROUP_CPU_ALLOCATED_FILE,
      const std::string& linux_cgroup_cpu_times_file = LINUX_CGROUP_CPU_TIMES_FILE,
      const std::string& cgroupv2_cpu_stat_file = LINUX_CGROUPV2_CPU_STAT_FILE,
      const std::string& cgroupv2_cpu_max_file = LINUX_CGROUPV2_CPU_MAX_FILE,
      const std::string& cgroupv2_cpu_effective_file = LINUX_CGROUPV2_CPU_EFFECTIVE_FILE);
  CpuTimes getCpuTimes() override;

private:
  TimeSource& time_source_;
  const std::string linux_cgroup_cpu_allocated_file_;
  const std::string linux_cgroup_cpu_times_file_;
  const std::string cgroupv2_cpu_stat_file_;
  const std::string cgroupv2_cpu_max_file_;
  const std::string cgroupv2_cpu_effective_file_;
};

} // namespace CpuUtilizationMonitor
} // namespace ResourceMonitors
} // namespace Extensions
} // namespace Envoy
