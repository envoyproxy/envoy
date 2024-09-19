#pragma once

#include <string>

#include "source/extensions/resource_monitors/cpu_utilization/cpu_stats_reader.h"

namespace Envoy {
namespace Extensions {
namespace ResourceMonitors {
namespace CpuUtilizationMonitor {

static const std::string LINUX_CPU_STATS_FILE = "/proc/stat";

class LinuxCpuStatsReader : public CpuStatsReader {
public:
  LinuxCpuStatsReader(const std::string& cpu_stats_filename = LINUX_CPU_STATS_FILE);
  CpuTimes getCpuTimes() override;

private:
  const std::string cpu_stats_filename_;
};

} // namespace CpuUtilizationMonitor
} // namespace ResourceMonitors
} // namespace Extensions
} // namespace Envoy
