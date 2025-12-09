#pragma once

#include <dirent.h>
#include <unistd.h>

#include <algorithm>
#include <filesystem>
#include <fstream>

#include "envoy/common/exception.h"

#include "source/common/common/fmt.h"
#include "source/common/common/logger.h"

#include "absl/strings/str_split.h"

namespace Envoy {
namespace Extensions {
namespace ResourceMonitors {
namespace CpuUtilizationMonitor {

struct CpuTimes {
  bool is_valid;
  bool is_cgroup_v2;
  double work_time; // For container cpu mode, to support normalisation of cgroup cpu usage stat per
                    // cpu core by dividing with available cpu limit
  uint64_t total_time;
  double effective_cores; // number of effective cores available to the container

  /**
   * Calculate CPU utilization based on the difference from previous CPU times.
   * This encapsulates the different calculation methods for cgroup v1 and v2.
   * @param previous_cpu_times The previous CpuTimes reading to calculate the delta.
   * @return The calculated CPU utilization as a fraction (0.0 to 1.0).
   * @throw EnvoyException if the delta values are invalid (negative work or non-positive total).
   */
  double calculateUtilization(const CpuTimes& previous_cpu_times) const {
    const double work_over_period = work_time - previous_cpu_times.work_time;
    const int64_t total_over_period = total_time - previous_cpu_times.total_time;

    if (work_over_period < 0 || total_over_period <= 0) {
      throw EnvoyException(
          fmt::format("Erroneous CPU stats calculation. Work_over_period='{}' cannot "
                      "be a negative number and total_over_period='{}' must be a positive number.",
                      work_over_period, total_over_period));
    }

    if (is_cgroup_v2) {
      const double total_over_period_seconds = total_over_period / 1000000000.0;
      const double utilization =
          ((work_over_period / 1000000.0) / (total_over_period_seconds * effective_cores));
      return std::clamp(utilization, 0.0, 1.0);
    } else {
      return work_over_period / total_over_period;
    }
  }
};

class CpuStatsReader {
public:
  CpuStatsReader() = default;
  virtual ~CpuStatsReader() = default;
  virtual CpuTimes getCpuTimes() PURE;
};

} // namespace CpuUtilizationMonitor
} // namespace ResourceMonitors
} // namespace Extensions
} // namespace Envoy
