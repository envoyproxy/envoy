#pragma once

#include <string>

#include "envoy/filesystem/filesystem.h"

#include "absl/strings/str_cat.h"

namespace Envoy {
namespace Extensions {
namespace ResourceMonitors {
namespace CgroupMemory {

/**
 * Utility class providing paths and detection methods for cgroup memory subsystem.
 */
struct CgroupPaths {
  /**
   * Paths and methods for cgroup v1 memory subsystem.
   */
  struct V1 {
    /**
     * @return The full path to the memory usage file.
     */
    static std::string getUsagePath() { return absl::StrCat(CGROUP_V1_BASE, USAGE); }

    /**
     * @return The full path to the memory limit file.
     */
    static std::string getLimitPath() { return absl::StrCat(CGROUP_V1_BASE, LIMIT); }

    /**
     * @return The base path for cgroup v1 memory subsystem.
     */
    static std::string getBasePath() { return CGROUP_V1_BASE; }

  private:
    // Base path for cgroup v1 memory subsystem.
    static constexpr const char* const CGROUP_V1_BASE = "/sys/fs/cgroup/memory";
    // File names for memory stats in cgroup v1.
    static constexpr const char* const USAGE = "/memory.usage_in_bytes";
    static constexpr const char* const LIMIT = "/memory.limit_in_bytes";
  };

  /**
   * Paths and methods for cgroup v2 memory subsystem.
   */
  struct V2 {
    /**
     * @return The full path to the memory usage file.
     */
    static std::string getUsagePath() { return absl::StrCat(CGROUP_V2_BASE, USAGE); }

    /**
     * @return The full path to the memory limit file.
     */
    static std::string getLimitPath() { return absl::StrCat(CGROUP_V2_BASE, LIMIT); }

  private:
    // Base path for cgroup v2 memory subsystem.
    static constexpr const char* const CGROUP_V2_BASE = "/sys/fs/cgroup";
    // File names for memory stats in cgroup v2.
    static constexpr const char* const USAGE = "/memory.current";
    static constexpr const char* const LIMIT = "/memory.max";
  };

  /**
   * @return Whether cgroup v2 memory subsystem is available.
   */
  static bool isV2(Filesystem::Instance& fs) {
    return fs.fileExists(V2::getUsagePath()) && fs.fileExists(V2::getLimitPath());
  }

  /**
   * @return Whether cgroup v1 memory subsystem is available.
   */
  static bool isV1(Filesystem::Instance& fs) { return fs.fileExists(V1::getBasePath()); }
};

} // namespace CgroupMemory
} // namespace ResourceMonitors
} // namespace Extensions
} // namespace Envoy
