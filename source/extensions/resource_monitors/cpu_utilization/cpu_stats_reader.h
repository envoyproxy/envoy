#pragma once

#include <dirent.h>
#include <unistd.h>

#include <algorithm>
#include <filesystem>
#include <fstream>

#include "envoy/common/exception.h"

#include "source/common/common/fmt.h"
#include "source/common/common/logger.h"

#include "absl/status/status.h"
#include "absl/status/statusor.h"
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
};

class CpuStatsReader {
public:
  CpuStatsReader() = default;
  virtual ~CpuStatsReader() = default;
  virtual CpuTimes getCpuTimes() PURE;

  /**
   * Update CPU statistics and calculate current utilization.
   * Each implementation tracks its own previous state internally and
   * performs implementation-specific calculation logic.
   * @return StatusOr containing utilization value (0.0 to 1.0) on success,
   *         or InvalidArgumentError if calculation fails.
   */
  virtual absl::StatusOr<double> getUtilization() PURE;
};

} // namespace CpuUtilizationMonitor
} // namespace ResourceMonitors
} // namespace Extensions
} // namespace Envoy
