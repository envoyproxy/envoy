#pragma once

#include <string>

#include "source/extensions/resource_monitors/envoy_container_cpu_utilization/container_stats_reader.h"

namespace Envoy {
namespace Extensions {
namespace ResourceMonitors {
namespace EnvoyContainerCpuUtilizationMonitor {

static const std::string LINUX_CGROUP_CPU_ALLOCATED_FILE = "/sys/fs/cgroup/cpu/cpu.shares";
static const std::string LINUX_CGROUP_CPU_TIMES_FILE = "/sys/fs/cgroup/cpu/cpuacct.usage";

class LinuxContainerStatsReader : public ContainerStatsReader {
public:
  LinuxContainerStatsReader(const std::string& linux_cgroup_cpu_allocated_file = LINUX_CGROUP_CPU_ALLOCATED_FILE,
  const std::string& linux_cgroup_cpu_times_file = LINUX_CGROUP_CPU_TIMES_FILE);
  EnvoyContainerStats getEnvoyContainerStats() override;

private:
  const std::string linux_cgroup_cpu_allocated_file_;
  const std::string linux_cgroup_cpu_times_file_;
};

} // namespace EnvoyContainerCpuUtilizationMonitor
} // namespace ResourceMonitors
} // namespace Extensions
} // namespace Envoy