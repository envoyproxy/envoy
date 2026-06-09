#pragma once

#include <string>

#include "envoy/filesystem/filesystem.h"

#include "absl/strings/str_cat.h"

namespace Envoy {
namespace Extensions {
namespace ResourceMonitors {
namespace CpuUtilizationMonitor {

/**
 * Utility class providing paths and detection methods for cgroup CPU subsystem.
 */
struct CpuPaths {

  struct V1 {
    // Returns the full path to the CPU shares file (cpu.shares).
    static std::string getSharesPath() { return absl::StrCat(CGROUP_V1_CPU_BASE, SHARES); }

    // Returns the full path to the CPU usage file (cpuacct.usage).
    static std::string getUsagePath() { return absl::StrCat(CGROUP_V1_CPUACCT_BASE, USAGE); }

    // Returns the base path for cgroup v1 CPU subsystem.
    static std::string getCpuBasePath() { return CGROUP_V1_CPU_BASE; }

    // Returns the base path for cgroup v1 cpuacct subsystem.
    static std::string getCpuacctBasePath() { return CGROUP_V1_CPUACCT_BASE; }

  private:
    // Base paths for cgroup v1 subsystems.
    static constexpr const char* const CGROUP_V1_CPU_BASE = "/sys/fs/cgroup/cpu";
    static constexpr const char* const CGROUP_V1_CPUACCT_BASE = "/sys/fs/cgroup/cpuacct";
    // File names for CPU stats in cgroup v1.
    static constexpr const char* const SHARES = "/cpu.shares";
    static constexpr const char* const USAGE = "/cpuacct.usage";
  };

  struct V2 {
    // Returns the full path to the CPU stat file (cpu.stat).
    static std::string getStatPath() { return absl::StrCat(CGROUP_V2_BASE, STAT); }

    // Returns the full path to the CPU max file (cpu.max).
    static std::string getMaxPath() { return absl::StrCat(CGROUP_V2_BASE, MAX); }

    // Returns the full path to the effective CPUs file (cpuset.cpus.effective).
    static std::string getEffectiveCpusPath() {
      return absl::StrCat(CGROUP_V2_BASE, EFFECTIVE_CPUS);
    }

  private:
    static constexpr const char* const CGROUP_V2_BASE = "/sys/fs/cgroup";
    static constexpr const char* const STAT = "/cpu.stat";
    static constexpr const char* const MAX = "/cpu.max";
    static constexpr const char* const EFFECTIVE_CPUS = "/cpuset.cpus.effective";
  };

  // Returns whether cgroup v2 CPU subsystem is available.
  static bool isV2(Filesystem::Instance& fs) {
    return fs.fileExists(V2::getStatPath()) && fs.fileExists(V2::getMaxPath()) &&
           fs.fileExists(V2::getEffectiveCpusPath());
  }

  // Returns whether cgroup v1 CPU subsystem is available.
  static bool isV1(Filesystem::Instance& fs) {
    return fs.fileExists(V1::getSharesPath()) && fs.fileExists(V1::getUsagePath());
  }
};

} // namespace CpuUtilizationMonitor
} // namespace ResourceMonitors
} // namespace Extensions
} // namespace Envoy
