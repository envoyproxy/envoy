#pragma once

#include <string>
#include <filesystem>

namespace Envoy {
namespace Extensions {
namespace ResourceMonitors {
namespace CgroupMemory {

struct CgroupPaths {
  static constexpr const char* const CGROUP_V1_BASE = "/sys/fs/cgroup/memory";
  static constexpr const char* const CGROUP_V2_BASE = "/sys/fs/cgroup";

  // Memory usage and limit paths for cgroup v1
  struct V1 {
    static constexpr const char* const USAGE = "/sys/fs/cgroup/memory/memory.usage_in_bytes";
    static constexpr const char* const LIMIT = "/sys/fs/cgroup/memory/memory.limit_in_bytes";
  };

  // Memory usage and limit paths for cgroup v2  
  struct V2 {
    static constexpr const char* const USAGE = "/sys/fs/cgroup/memory.current";
    static constexpr const char* const LIMIT = "/sys/fs/cgroup/memory.max";
  };

  // Helper methods to check cgroup version
  static bool isV2() {
    return std::filesystem::exists(V2::USAGE) && 
           std::filesystem::exists(V2::LIMIT);
  }

  static bool isV1() {
    return std::filesystem::exists(CGROUP_V1_BASE);
  } 
};

} // namespace CgroupMemory
} // namespace ResourceMonitors
} // namespace Extensions
} // namespace Envoy