#pragma once

#include "envoy/common/pure.h"

#include "absl/status/statusor.h"

namespace Envoy {
namespace Extensions {
namespace ResourceMonitors {
namespace CpuUtilizationMonitor {

class CpuStatsReader {
public:
  CpuStatsReader() = default;
  virtual ~CpuStatsReader() = default;

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
