// Container-aware CPU detection utility for Envoy
// Inspired by Go's runtime cgroup CPU limit detection
// See: https://github.com/golang/go/blob/go1.23.4/src/internal/cgroup/cgroup_linux.go

#include "source/server/cgroup_cpu_util.h"

#include <cmath>

#include "source/common/common/logger.h"

#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_split.h"
#include "absl/strings/strip.h"

namespace Envoy {

// Returns the CPU limit from cgroup subsystem, following Go runtime behavior.
// This function prioritizes cgroup v1 over v2 when both are available,
// as v1 CPU controllers take precedence in hybrid environments.
//
// Return values:
//   Valid uint32_t: Actual CPU limit (number of CPUs, rounded up)
//   absl::nullopt: No limit detected (unlimited CPU usage allowed)
//
// Algorithm:
//   1. Check cgroup v2 first (modern unified hierarchy)
//   2. Check cgroup v1 (legacy but takes precedence if CPU controller present)
//   3. Return the minimum limit if both are found
//   4. Return absl::nullopt if limits exist but are unlimited
//   5. Return absl::nullopt if no cgroup system is available
absl::optional<uint32_t> CgroupCpuUtil::getCpuLimit(Filesystem::Instance& fs) {
  absl::optional<uint32_t> effective_limit;

  // Check cgroup v2 first (modern unified hierarchy)
  auto v2_limit = getCgroupV2CpuLimit(fs);
  if (v2_limit.has_value()) {
    effective_limit = v2_limit;
  }

  // cgroup v1 CPU controller takes precedence over v2 when both exist.
  // This matches Go runtime behavior and Linux kernel precedence rules.
  auto v1_limit = getCgroupV1CpuLimit(fs);
  if (v1_limit.has_value()) {
    effective_limit = v1_limit;
  }

  return effective_limit;
}

// Reads CPU limits from cgroup v2 unified hierarchy.
// cgroup v2 uses a single "cpu.max" file with format "quota period"
// where quota may be "max" for unlimited.
//
// IMPORTANT: We read from the leaf cgroup where the process is actually constrained.
// From Go's runtime cgroup implementation
// (https://github.com/golang/go/blob/go1.23.4/src/internal/cgroup/cgroup_linux.go#L154): "We only
// read the limit from the leaf cgroup that actually contains this process. But a parent cgroup may
// have a tighter limit. That tighter limit would be our effective limit. That said, container
// runtimes tend to hide parent cgroup from the container anyway."
//
// This approach matches Go's runtime behavior and handles the race condition where
// cgroup membership changes during detection.
absl::optional<uint32_t> CgroupCpuUtil::getCgroupV2CpuLimit(Filesystem::Instance& fs) {
  // Parse /proc/self/cgroup to find our position in the cgroup hierarchy
  // Format: "0::/path/to/cgroup" for v2
  std::string cgroup_path = getCurrentCgroupPath(fs);
  if (cgroup_path.empty()) {
    // Fallback: Check root cgroup (may miss container-specific limits)
    return readCgroupV2CpuLimit(fs, std::string(CGROUP_V2_CPU_MAX));
  }

  // Read from the leaf cgroup where this process is actually constrained
  // This is crucial for container environments where limits are set per-pod
  std::string cpu_max_path = absl::StrCat(CGROUP_V2_BASE_PATH, cgroup_path, "/cpu.max");
  return readCgroupV2CpuLimit(fs, cpu_max_path);
}

// Reads CPU limits from cgroup v1 legacy hierarchy.
// cgroup v1 uses separate files: cpu.cfs_quota_us and cpu.cfs_period_us
// where quota of -1 indicates unlimited.
//
// IMPORTANT: We read from the leaf cgroup where the process is actually constrained.
// This is critical because:
// 1. Process migration: If a process is migrated out of the cgroup found by parsing
//    /proc/self/cgroup and that cgroup is deleted, reading parent cgroup would fail
// 2. Container isolation: In Kubernetes/Docker, limits are set on the leaf cgroup
//    (per-pod/per-container), not on parent cgroup in the hierarchy
// 3. Accuracy: Parent cgroup may have different (usually higher) limits that don't
//    reflect the actual constraints applied to this specific process
//
// Note: v1 CPU controller takes precedence over v2 when both are present,
// following Linux kernel behavior and Go runtime convention.
absl::optional<uint32_t> CgroupCpuUtil::getCgroupV1CpuLimit(Filesystem::Instance& fs) {
  // Parse /proc/self/cgroup to find CPU controller mount point
  // Format: "N:cpu:/path/to/cgroup" for v1 with CPU controller
  std::string cgroup_path = getCurrentCgroupPath(fs);
  if (cgroup_path.empty()) {
    // Fallback: Check root cgroup (may miss container-specific limits)
    return readCgroupV1CpuLimit(fs, std::string(CGROUP_V1_CPU_QUOTA),
                                std::string(CGROUP_V1_CPU_PERIOD));
  }

  // Read from the leaf cgroup where this process is actually constrained
  std::string quota_path = absl::StrCat(CGROUP_V1_BASE_PATH, cgroup_path, "/cpu.cfs_quota_us");
  std::string period_path = absl::StrCat(CGROUP_V1_BASE_PATH, cgroup_path, "/cpu.cfs_period_us");
  return readCgroupV1CpuLimit(fs, quota_path, period_path);
}

// Parses cgroup v2 cpu.max file to extract CPU limit.
// File format: "<quota> <period>" or "max <period>" for unlimited
//
// Examples:
//   "150000 100000" = 1.5 CPUs (150ms quota per 100ms period)
//   "max 100000"    = unlimited
//   "200000 100000" = 2.0 CPUs (200ms quota per 100ms period)
//
// Returns absl::nullopt for unlimited or parsing errors, valid value for actual limit (rounded up).
absl::optional<uint32_t> CgroupCpuUtil::readCgroupV2CpuLimit(Filesystem::Instance& fs,
                                                             const std::string& cpu_max_path) {
  const auto result = fs.fileReadToEnd(cpu_max_path);
  if (!result.ok()) {
    // File doesn't exist or can't be read - cgroup v2 not available
    return absl::nullopt;
  }

  const std::string content = std::string(absl::StripAsciiWhitespace(result.value()));

  // Expected format: "quota period" where both are microseconds
  const std::vector<std::string> parts = absl::StrSplit(content, ' ');
  if (parts.size() != 2) {
    // Malformed file - log warning and return nullopt
    ENVOY_LOG_MISC(warn,
                   "Malformed cgroup v2 cpu.max file {}: expected 'quota period' format, got '{}'",
                   cpu_max_path, content);
    return absl::nullopt;
  }

  // Check if quota is "max" (unlimited CPU)
  if (parts[0] == "max") {
    return absl::nullopt; // Unlimited
  }

  uint64_t quota, period;
  if (!absl::SimpleAtoi(parts[0], &quota) || !absl::SimpleAtoi(parts[1], &period)) {
    // Parse failure - log warning and return nullopt
    ENVOY_LOG_MISC(warn, "Failed to parse cgroup v2 cpu.max file {}: quota='{}' period='{}'",
                   cpu_max_path, parts[0], parts[1]);
    return absl::nullopt;
  }

  if (period == 0) {
    // Division by zero protection
    ENVOY_LOG_MISC(warn, "Invalid cgroup v2 cpu.max file {}: period cannot be zero", cpu_max_path);
    return absl::nullopt;
  }

  // Calculate CPU limit as quota/period ratio, rounded up to next integer
  // This matches Go's behavior: 1.5 CPUs becomes 2 CPUs for worker threads
  const uint32_t cpu_limit = static_cast<uint32_t>(std::ceil(static_cast<double>(quota) / period));
  return cpu_limit > 0 ? cpu_limit : 1; // Ensure at least 1 CPU
}

// Parses cgroup v1 CPU quota and period files to extract CPU limit.
// Uses separate files: cpu.cfs_quota_us and cpu.cfs_period_us
//
// Examples:
//   quota=150000, period=100000 = 1.5 CPUs
//   quota=-1,     period=100000 = unlimited
//   quota=200000, period=100000 = 2.0 CPUs
//
// Returns absl::nullopt for unlimited or parsing errors, valid value for actual limit (rounded up).
absl::optional<uint32_t> CgroupCpuUtil::readCgroupV1CpuLimit(Filesystem::Instance& fs,
                                                             const std::string& quota_path,
                                                             const std::string& period_path) {
  // Read the quota file (cpu.cfs_quota_us)
  const auto quota_result = fs.fileReadToEnd(quota_path);
  if (!quota_result.ok()) {
    // File doesn't exist or can't be read - cgroup v1 not available
    return absl::nullopt;
  }

  // Read the period file (cpu.cfs_period_us)
  const auto period_result = fs.fileReadToEnd(period_path);
  if (!period_result.ok()) {
    // File doesn't exist or can't be read - cgroup v1 not available
    return absl::nullopt;
  }

  const std::string quota_str = std::string(absl::StripAsciiWhitespace(quota_result.value()));
  const std::string period_str = std::string(absl::StripAsciiWhitespace(period_result.value()));

  int64_t quota, period;
  if (!absl::SimpleAtoi(quota_str, &quota) || !absl::SimpleAtoi(period_str, &period)) {
    // Parse failure - log warning and return nullopt
    ENVOY_LOG_MISC(warn, "Failed to parse cgroup v1 CPU files: quota='{}' period='{}'", quota_str,
                   period_str);
    return absl::nullopt;
  }

  // Check if quota is -1 (standard cgroup v1 unlimited indicator)
  if (quota == -1) {
    return absl::nullopt; // Unlimited
  }

  if (period <= 0 || quota <= 0) {
    // Invalid values - log warning and return nullopt
    ENVOY_LOG_MISC(warn, "Invalid cgroup v1 CPU values: quota={} period={}", quota, period);
    return absl::nullopt;
  }

  // Calculate CPU limit as quota/period ratio, rounded up to next integer
  // This matches Go's behavior: 1.5 CPUs becomes 2 CPUs for worker threads
  const uint32_t cpu_limit = static_cast<uint32_t>(std::ceil(static_cast<double>(quota) / period));
  return cpu_limit > 0 ? cpu_limit : 1; // Ensure at least 1 CPU
}

// Parses /proc/self/cgroup to find the current process's cgroup path.
// This is critical for container environments where processes are placed
// in specific cgroup with their own resource limits.
//
// File format (one line per hierarchy):
//   cgroup v2: "0::/path/to/cgroup"
//   cgroup v1: "N:controller,list:/path/to/cgroup"
//
// We prioritize finding a v1 CPU controller, falling back to v2 unified hierarchy.
// This matches Linux kernel precedence and Go runtime behavior.
//
// Returns empty string if no suitable cgroup path is found.
std::string CgroupCpuUtil::getCurrentCgroupPath(Filesystem::Instance& fs) {
  const auto result = fs.fileReadToEnd(std::string(PROC_CGROUP_PATH));
  if (!result.ok()) {
    // /proc/self/cgroup doesn't exist - not in a cgroup
    return "";
  }

  const std::string content = result.value();
  const std::vector<std::string> lines = absl::StrSplit(content, '\n');

  // First pass: Look for cgroup v1 CPU controller (takes precedence)
  for (const std::string& line : lines) {
    if (line.empty())
      continue;

    const std::vector<std::string> parts = absl::StrSplit(line, ':');
    if (parts.size() != 3) {
      // This is expected for some lines in /proc/self/cgroup, not all follow the format
      continue;
    }

    // Check for v1 hierarchy with CPU controller
    if (parts[0] != "0" && parts[1].find("cpu") != std::string::npos) {
      // Found cgroup v1 with CPU controller - this takes precedence
      return parts[2];
    }
  }

  // Second pass: Look for cgroup v2 unified hierarchy
  for (const std::string& line : lines) {
    if (line.empty())
      continue;

    const std::vector<std::string> parts = absl::StrSplit(line, ':');
    if (parts.size() != 3) {
      // This is expected for some lines in /proc/self/cgroup, not all follow the format
      continue;
    }

    // Check for v2 unified hierarchy
    if (parts[0] == "0" && parts[1].empty()) {
      // Found cgroup v2 unified hierarchy
      return parts[2];
    }
  }

  // No suitable cgroup found
  return "";
}

} // namespace Envoy
