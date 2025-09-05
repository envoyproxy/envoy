#pragma once

#include <cstdint>

#include "envoy/filesystem/filesystem.h"

namespace Envoy {

/**
 * Utility class for detecting CPU limits from cgroup subsystem.
 */
class CgroupCpuUtil {
public:
  /**
   * Detects CPU limit from cgroup v2 or v1 with hierarchy scanning, falling back to hardware count.
   * Scans cgroup hierarchy and takes minimum effective limit for container-aware CPU detection.
   * @param fs Filesystem instance for file operations.
   * @param hw_threads Hardware thread count as fallback.
   * @return CPU limit or hw_threads if no cgroup limit found.
   */
  static uint32_t getCpuLimit(Filesystem::Instance& fs, uint32_t hw_threads);

private:
  /**
   * Reads CPU limit from cgroup v2 cpu.max file with hierarchy scanning.
   * @param fs Filesystem instance.
   * @return CPU limit or 0 if not available/unlimited.
   */
  static uint32_t getCgroupV2CpuLimit(Filesystem::Instance& fs);

  /**
   * Reads CPU limit from cgroup v1 cpu.cfs_quota_us and cpu.cfs_period_us with hierarchy scanning.
   * @param fs Filesystem instance.
   * @return CPU limit or 0 if not available/unlimited.
   */
  static uint32_t getCgroupV1CpuLimit(Filesystem::Instance& fs);

  /**
   * Reads CPU limit from a specific cgroup v2 path.
   * @param fs Filesystem instance.
   * @param cpu_max_path Path to cpu.max file.
   * @return CPU limit or 0 if not available/unlimited.
   */
  static uint32_t readCgroupV2CpuLimit(Filesystem::Instance& fs, const std::string& cpu_max_path);

  /**
   * Reads CPU limit from specific cgroup v1 paths.
   * @param fs Filesystem instance.
   * @param quota_path Path to cpu.cfs_quota_us file.
   * @param period_path Path to cpu.cfs_period_us file.
   * @return CPU limit or 0 if not available/unlimited.
   */
  static uint32_t readCgroupV1CpuLimit(Filesystem::Instance& fs, const std::string& quota_path,
                                       const std::string& period_path);

  /**
   * Gets the current process cgroup path for hierarchy scanning.
   * @param fs Filesystem instance.
   * @return Cgroup path or empty string if not found.
   */
  static std::string getCurrentCgroupPath(Filesystem::Instance& fs);

  /**
   * Checks if cgroup v2 CPU files exist.
   */
  static bool isV2Available(Filesystem::Instance& fs);

  /**
   * Checks if cgroup v1 CPU files exist.
   */
  static bool isV1Available(Filesystem::Instance& fs);

  // Cgroup v2 paths
  static constexpr const char* const CGROUP_V2_CPU_MAX = "/sys/fs/cgroup/cpu.max";
  static constexpr const char* const CGROUP_V2_BASE_PATH = "/sys/fs/cgroup";

  // Cgroup v1 paths
  static constexpr const char* const CGROUP_V1_CPU_QUOTA = "/sys/fs/cgroup/cpu/cpu.cfs_quota_us";
  static constexpr const char* const CGROUP_V1_CPU_PERIOD = "/sys/fs/cgroup/cpu/cpu.cfs_period_us";
  static constexpr const char* const CGROUP_V1_BASE_PATH = "/sys/fs/cgroup/cpu";

  // Process cgroup info
  static constexpr const char* const PROC_CGROUP_PATH = "/proc/self/cgroup";
};

} // namespace Envoy
