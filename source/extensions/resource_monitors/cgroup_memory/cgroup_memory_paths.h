#pragma once

#include <filesystem>
#include <string>

#include "envoy/common/pure.h"

namespace Envoy {
namespace Extensions {
namespace ResourceMonitors {
namespace CgroupMemory {

/**
 * Interface for filesystem operations to support testing.
 */
class FileSystem {
public:
  virtual ~FileSystem() = default;
  virtual bool exists(const std::string& path) const PURE;

  /**
   * @return A singleton instance of the filesystem implementation.
   */
  static const FileSystem& instance() { return *instance_; }

  /**
   * For testing: Sets the global filesystem instance.
   * @param instance The filesystem instance to use.
   */
  static void setInstance(const FileSystem* instance) { instance_ = instance; }

private:
  static const FileSystem* instance_;
};

/**
 * Utility class providing paths and detection methods for cgroup memory subsystem.
 */
struct CgroupPaths {
  // Base paths for cgroup memory subsystem.
  static constexpr const char* const CGROUP_V1_BASE = "/sys/fs/cgroup/memory";
  static constexpr const char* const CGROUP_V2_BASE = "/sys/fs/cgroup";

  /**
   * Paths and methods for cgroup v1 memory subsystem.
   */
  struct V1 {
    static constexpr const char* const USAGE = "/memory.usage_in_bytes";
    static constexpr const char* const LIMIT = "/memory.limit_in_bytes";

    /**
     * @return The full path to the memory usage file.
     */
    static std::string getUsagePath() { return std::string(CGROUP_V1_BASE) + USAGE; }

    /**
     * @return The full path to the memory limit file.
     */
    static std::string getLimitPath() { return std::string(CGROUP_V1_BASE) + LIMIT; }
  };

  /**
   * Paths and methods for cgroup v2 memory subsystem.
   */
  struct V2 {
    static constexpr const char* const USAGE = "/memory.current";
    static constexpr const char* const LIMIT = "/memory.max";

    /**
     * @return The full path to the memory usage file.
     */
    static std::string getUsagePath() { return std::string(CGROUP_V2_BASE) + USAGE; }

    /**
     * @return The full path to the memory limit file.
     */
    static std::string getLimitPath() { return std::string(CGROUP_V2_BASE) + LIMIT; }
  };

  /**
   * @return Whether cgroup v2 memory subsystem is available.
   */
  static bool isV2() {
    return FileSystem::instance().exists(V2::getUsagePath()) &&
           FileSystem::instance().exists(V2::getLimitPath());
  }

  /**
   * @return Whether cgroup v1 memory subsystem is available.
   */
  static bool isV1() { return FileSystem::instance().exists(CGROUP_V1_BASE); }
};

} // namespace CgroupMemory
} // namespace ResourceMonitors
} // namespace Extensions
} // namespace Envoy
