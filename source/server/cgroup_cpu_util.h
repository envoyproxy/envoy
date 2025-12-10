#pragma once

#include <cstdint>

#include "envoy/filesystem/filesystem.h"

#include "source/common/singleton/threadsafe_singleton.h"

#include "absl/strings/string_view.h"
#include "absl/types/optional.h"

namespace Envoy {

/**
 * Interface for cgroup CPU detection. Allows mocking in tests.
 */
class CgroupDetector {
public:
  virtual ~CgroupDetector() = default;

  /**
   * Detects CPU limit from `cgroup` subsystem.
   * @param fs Filesystem instance for file operations.
   * @return CPU limit or absl::nullopt if no `cgroup` limit found.
   */
  virtual absl::optional<uint32_t> getCpuLimit(Filesystem::Instance& fs) PURE;
};

/**
 * Production implementation of cgroup CPU detection.
 */
class CgroupDetectorImpl : public CgroupDetector {
public:
  absl::optional<uint32_t> getCpuLimit(Filesystem::Instance& fs) override;
};

using CgroupDetectorSingleton = ThreadSafeSingleton<CgroupDetectorImpl>;

/**
 * `Cgroup` filesystem mount information.
 */
struct CgroupMount {
  std::string mount_point;
  std::string filesystem_type;
  std::string mount_options;
  bool has_cpu_controller = false;
};

/**
 * `Cgroup` path information with version detection from `/proc/self/cgroup` parsing.
 * Follows Envoy's pattern like LegacyLbPolicyConfigHelper::Result.
 */
struct CgroupPathInfo {
  std::string relative_path; // Relative `cgroup` path like "/docker/abc123" or "/"
  std::string version;       // "v1" or "v2" detected from hierarchy parsing

  // Constructor for easy creation
  CgroupPathInfo(std::string path, std::string ver)
      : relative_path(std::move(path)), version(std::move(ver)) {}
};

/**
 * Combined `cgroup` information with path and version.
 */
struct CgroupInfo {
  std::string full_path; // Combined mount + relative path
  std::string version;   // "v1" or "v2"
};

/**
 * CPU `cgroup` files with cached file content.
 */
struct CpuFiles {
  std::string version;
  std::string quota_content;  // Store actual file content, not path
  std::string period_content; // Store actual file content, not path (empty for v2)
};

/**
 * Utility class for detecting CPU limits from `cgroup` subsystem.
 */
class CgroupCpuUtil {
public:
  class TestUtil;
  friend class TestUtil;
  /**
   * Detects CPU limit from `cgroup` `v2` or `v1` with hierarchy scanning.
   * Scans `cgroup` hierarchy and takes minimum effective limit for container-aware CPU detection.
   * @param fs Filesystem instance for file operations.
   * @return CPU limit or absl::nullopt if no `cgroup` limit found.
   */
  static absl::optional<uint32_t> getCpuLimit(Filesystem::Instance& fs);

private:
  /**
   * Reads CPU limit from specific `cgroup` `v1` paths.
   * @param fs Filesystem instance.
   * @param quota_path Path to `cpu.cfs_quota_us` file.
   * @param period_path Path to `cpu.cfs_period_us` file.
   * @return CPU limit or absl::nullopt if not available/unlimited.
   */

  // Validates `cgroup` file content following Go's strict requirements:
  // - Content must end with newline (matching Go's validation)
  // Returns string_view without trailing newline on success, nullopt on failure.
  static absl::optional<absl::string_view> validateCgroupFileContent(const std::string& content,
                                                                     const std::string& file_path);

  // Parses `/proc/self/cgroup` to find the current process's `cgroup` path with priority handling.

  /**
   * Gets the current process `cgroup` path and version by parsing `/proc/self/cgroup`.
   * Determines version from hierarchy ID and controller info.
   * @param fs Filesystem instance.
   * @return CgroupPathInfo with relative path and version, nullopt if not found.
   */
  static absl::optional<CgroupPathInfo> getCurrentCgroupPath(Filesystem::Instance& fs);

  /**
   * Discovers cgroup filesystem mounts by parsing `/proc/self/mountinfo`.
   * Priority handling: `cgroup` `v1` with CPU controller wins over `cgroup` `v2`.
   * @param fs Filesystem instance.
   * @return Mount point string on success, nullopt if no suitable `cgroup` found.
   */
  static absl::optional<std::string> discoverCgroupMount(Filesystem::Instance& fs);

  /**
   * Parses a single line from `/proc/self/mountinfo` to extract cgroup mount point.
   * Format: `mountID parentID major:minor root mountPoint options - fsType source superOptions`
   * We extract field 5 (mount point) for `cgroup`/`cgroup2` filesystem only.
   * @param line Single line from `/proc/self/mountinfo`
   * @return Mount point string if line contains `cgroup` filesystem, nullopt if not a `cgroup`
   * line.
   */
  static absl::optional<std::string> parseMountInfoLine(const std::string& line);

  /**
   * Unescapes octal escape sequences in paths from `/proc/self/mountinfo`.
   * Linux's `show_path` converts `\`, ` `, `\t`, and `\n` to octal escape sequences
   * like `\040` for space, `\134` for backslash.
   * @param path The escaped path string from `mountinfo`.
   * @return The unescaped path string.
   */
  static std::string unescapePath(const std::string& path);

  /**
   * Constructs complete `cgroup` path by combining mount point and process assignment.
   * Logic: Use provided mount point (already discovered)
   *        Call process assignment → Get relative path
   *        Combine mount point and relative path
   * @param mount_point The `cgroup` mount point (from discoverCgroupMount).
   * @param fs Filesystem instance.
   * @return CgroupInfo with combined path + final version, nullopt if not found.
   */
  static absl::optional<CgroupInfo> constructCgroupPath(const std::string& mount_point,
                                                        Filesystem::Instance& fs);

  /**
   * Accesses `cgroup` CPU files with version-specific filename appending.
   * Logic: Get combined path from Step 3
   *        Append version-specific filenames (`v1`: quota+period, `v2`: `cpu.max`)
   *        Open files with `O_RDONLY`|`O_CLOEXEC` flags
   *        Buffer reuse: Same buffer for different file paths
   *        Error handling: File not found → return nullopt
   * @param cgroup_info Combined path and version from step 3.
   * @param fs Filesystem instance.
   * @return CpuFiles struct with cached file content, nullopt if files not accessible.
   */
  static absl::optional<CpuFiles> accessCgroupFiles(const CgroupInfo& cgroup_info,
                                                    Filesystem::Instance& fs);

  /**
   * Accesses `cgroup` `v1` CPU files (quota and period).
   * @param cgroup_info Combined path and version from step 3.
   * @param fs Filesystem instance.
   * @return CpuFiles struct with cached file content, nullopt if files not accessible.
   */
  static absl::optional<CpuFiles> accessCgroupV1Files(const CgroupInfo& cgroup_info,
                                                      Filesystem::Instance& fs);

  /**
   * Accesses `cgroup` `v2` CPU file (`cpu.max`).
   * @param cgroup_info Combined path and version from step 3.
   * @param fs Filesystem instance.
   * @return CpuFiles struct with cached file content, nullopt if files not accessible.
   */
  static absl::optional<CpuFiles> accessCgroupV2Files(const CgroupInfo& cgroup_info,
                                                      Filesystem::Instance& fs);

  /**
   * Reads actual CPU limits from `cgroup` files with version-specific parsing.
   * Logic: Use cached file paths from Step 4
   *        Read from offset 0 for fresh data
   *        Version-specific parsing (`v1`: divide quota/period, `v2`: parse "quota period")
   *        Handle special cases (`v1`: quota=-1, `v2`: quota="max" means no limit)
   * @param cpu_files Cached file paths from step 4.
   * @param fs Filesystem instance.
   * @return CPU limit as float64 ratio, nullopt if unlimited/invalid.
   */
  static absl::optional<double> readActualLimits(const CpuFiles& cpu_files,
                                                 Filesystem::Instance& fs);

  /**
   * Reads actual CPU limits from `cgroup` `v1` files with quota/period parsing.
   * @param cpu_files Cached file content from `v1` files.
   * @return CPU limit as float64 ratio, nullopt if unlimited/invalid.
   */
  static absl::optional<double> readActualLimitsV1(const CpuFiles& cpu_files);

  /**
   * Reads actual CPU limits from `cgroup` `v2` files with "quota period" parsing.
   * @param cpu_files Cached file content from `v2` files.
   * @return CPU limit as float64 ratio, nullopt if unlimited/invalid.
   */
  static absl::optional<double> readActualLimitsV2(const CpuFiles& cpu_files);

  // `Cgroup` `v2` paths
  static constexpr absl::string_view CGROUP_V2_CPU_MAX = "/sys/fs/cgroup/cpu.max";
  static constexpr absl::string_view CGROUP_V2_BASE_PATH = "/sys/fs/cgroup";

  // `Cgroup` `v1` paths
  static constexpr absl::string_view CGROUP_V1_CPU_QUOTA = "/sys/fs/cgroup/cpu/cpu.cfs_quota_us";
  static constexpr absl::string_view CGROUP_V1_CPU_PERIOD = "/sys/fs/cgroup/cpu/cpu.cfs_period_us";
  static constexpr absl::string_view CGROUP_V1_BASE_PATH = "/sys/fs/cgroup/cpu";

  // Process `cgroup` info
  static constexpr absl::string_view PROC_CGROUP_PATH = "/proc/self/cgroup";
  static constexpr absl::string_view PROC_MOUNTINFO_PATH = "/proc/self/mountinfo";

  // `Cgroup` filename constants
  static constexpr absl::string_view CGROUP_V1_QUOTA_FILE = "/cpu.cfs_quota_us";
  static constexpr absl::string_view CGROUP_V1_PERIOD_FILE = "/cpu.cfs_period_us";
  static constexpr absl::string_view CGROUP_V2_CPU_MAX_FILE = "/cpu.max";
};

/**
 * Test utility class to provide access to private methods.
 */
class CgroupCpuUtil::TestUtil {
public:
  static absl::optional<CgroupPathInfo> getCurrentCgroupPath(Filesystem::Instance& fs) {
    return CgroupCpuUtil::getCurrentCgroupPath(fs);
  }

  static absl::optional<std::string> discoverCgroupMount(Filesystem::Instance& fs) {
    return CgroupCpuUtil::discoverCgroupMount(fs);
  }

  static absl::optional<std::string> parseMountInfoLine(const std::string& line) {
    return CgroupCpuUtil::parseMountInfoLine(line);
  }

  static std::string unescapePath(const std::string& path) {
    return CgroupCpuUtil::unescapePath(path);
  }

  static absl::optional<absl::string_view> validateCgroupFileContent(const std::string& content,
                                                                     const std::string& file_path) {
    return CgroupCpuUtil::validateCgroupFileContent(content, file_path);
  }

  // TestUtil wrappers for our new `modularized` functions
  static absl::optional<CpuFiles> accessCgroupV1Files(const CgroupInfo& cgroup_info,
                                                      Filesystem::Instance& fs) {
    return CgroupCpuUtil::accessCgroupV1Files(cgroup_info, fs);
  }

  static absl::optional<CpuFiles> accessCgroupV2Files(const CgroupInfo& cgroup_info,
                                                      Filesystem::Instance& fs) {
    return CgroupCpuUtil::accessCgroupV2Files(cgroup_info, fs);
  }

  static absl::optional<double> readActualLimitsV1(const CpuFiles& cpu_files) {
    return CgroupCpuUtil::readActualLimitsV1(cpu_files);
  }

  static absl::optional<double> readActualLimitsV2(const CpuFiles& cpu_files) {
    return CgroupCpuUtil::readActualLimitsV2(cpu_files);
  }
};

} // namespace Envoy
