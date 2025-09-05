#include "source/server/cgroup_cpu_util.h"

#include <climits>
#include <cmath>

#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_split.h"
#include "absl/strings/strip.h"

namespace Envoy {

uint32_t CgroupCpuUtil::getCpuLimit(Filesystem::Instance& fs, uint32_t hw_threads) {
  uint32_t min_limit = hw_threads;

  // Try cgroup v2 first with hierarchy scanning
  if (isV2Available(fs)) {
    uint32_t v2_limit = getCgroupV2CpuLimit(fs);
    if (v2_limit > 0 && v2_limit < min_limit) {
      min_limit = v2_limit;
    }
  }

  // Try cgroup v1 with hierarchy scanning
  if (isV1Available(fs)) {
    uint32_t v1_limit = getCgroupV1CpuLimit(fs);
    if (v1_limit > 0 && v1_limit < min_limit) {
      min_limit = v1_limit;
    }
  }

  // Return minimum of 1 CPU
  return std::max(1U, min_limit);
}

uint32_t CgroupCpuUtil::getCgroupV2CpuLimit(Filesystem::Instance& fs) {
  // Get current cgroup path from /proc/self/cgroup
  std::string cgroup_path = getCurrentCgroupPath(fs);
  if (cgroup_path.empty()) {
    // Fall back to checking just the root
    return readCgroupV2CpuLimit(fs, CGROUP_V2_CPU_MAX);
  }

  // Read only the leaf cgroup for simplicity and stability
  std::string current_path = absl::StrCat(CGROUP_V2_BASE_PATH, cgroup_path);
  std::string cpu_max_path = absl::StrCat(current_path, "/cpu.max");
  return readCgroupV2CpuLimit(fs, cpu_max_path);
}

uint32_t CgroupCpuUtil::getCgroupV1CpuLimit(Filesystem::Instance& fs) {
  // Get current cgroup path from /proc/self/cgroup
  std::string cgroup_path = getCurrentCgroupPath(fs);
  if (cgroup_path.empty()) {
    // Fall back to checking just the root
    return readCgroupV1CpuLimit(fs, CGROUP_V1_CPU_QUOTA, CGROUP_V1_CPU_PERIOD);
  }

  // Read only the leaf cgroup for simplicity and stability
  std::string current_path = absl::StrCat(CGROUP_V1_BASE_PATH, cgroup_path);
  std::string quota_path = absl::StrCat(current_path, "/cpu.cfs_quota_us");
  std::string period_path = absl::StrCat(current_path, "/cpu.cfs_period_us");
  return readCgroupV1CpuLimit(fs, quota_path, period_path);
}

uint32_t CgroupCpuUtil::readCgroupV2CpuLimit(Filesystem::Instance& fs,
                                             const std::string& cpu_max_path) {
  const auto result = fs.fileReadToEnd(cpu_max_path);
  if (!result.ok()) {
    return 0;
  }

  const std::string content = std::string(absl::StripAsciiWhitespace(result.value()));

  // Format is "quota period" or "max period" for unlimited
  const std::vector<std::string> parts = absl::StrSplit(content, ' ');
  if (parts.size() != 2) {
    return 0;
  }

  // Check if quota is "max" (unlimited)
  if (parts[0] == "max") {
    return 0;
  }

  uint64_t quota, period;
  if (!absl::SimpleAtoi(parts[0], &quota) || !absl::SimpleAtoi(parts[1], &period)) {
    return 0;
  }

  if (period == 0) {
    return 0;
  }

  // Calculate CPU limit: quota / period, rounded up
  const uint32_t cpu_limit = static_cast<uint32_t>(std::ceil(static_cast<double>(quota) / period));
  return cpu_limit > 0 ? cpu_limit : 1; // Ensure at least 1 CPU
}

uint32_t CgroupCpuUtil::readCgroupV1CpuLimit(Filesystem::Instance& fs,
                                             const std::string& quota_path,
                                             const std::string& period_path) {
  // Read quota file
  const auto quota_result = fs.fileReadToEnd(quota_path);
  if (!quota_result.ok()) {
    return 0;
  }

  // Read period file
  const auto period_result = fs.fileReadToEnd(period_path);
  if (!period_result.ok()) {
    return 0;
  }

  const std::string quota_str = std::string(absl::StripAsciiWhitespace(quota_result.value()));
  const std::string period_str = std::string(absl::StripAsciiWhitespace(period_result.value()));

  int64_t quota, period;
  if (!absl::SimpleAtoi(quota_str, &quota) || !absl::SimpleAtoi(period_str, &period)) {
    return 0;
  }

  // Check if quota is -1 (unlimited)
  if (quota == -1) {
    return 0;
  }

  if (period <= 0 || quota <= 0) {
    return 0;
  }

  // Calculate CPU limit: quota / period, rounded up
  const uint32_t cpu_limit = static_cast<uint32_t>(std::ceil(static_cast<double>(quota) / period));
  return cpu_limit > 0 ? cpu_limit : 1; // Ensure at least 1 CPU
}

std::string CgroupCpuUtil::getCurrentCgroupPath(Filesystem::Instance& fs) {
  const auto result = fs.fileReadToEnd(PROC_CGROUP_PATH);
  if (!result.ok()) {
    return "";
  }

  const std::string content = result.value();
  const std::vector<std::string> lines = absl::StrSplit(content, '\n');

  for (const std::string& line : lines) {
    if (line.empty())
      continue;

    const std::vector<std::string> parts = absl::StrSplit(line, ':');
    if (parts.size() != 3)
      continue;

    // For cgroup v2, look for "0::" line
    // For cgroup v1, look for lines containing "cpu"
    if (parts[0] == "0" && parts[1].empty()) {
      // This is cgroup v2
      return parts[2];
    } else if (parts[1].find("cpu") != std::string::npos) {
      // This is cgroup v1 with cpu controller
      return parts[2];
    }
  }

  return "";
}

bool CgroupCpuUtil::isV2Available(Filesystem::Instance& fs) {
  return fs.fileExists(CGROUP_V2_CPU_MAX);
}

bool CgroupCpuUtil::isV1Available(Filesystem::Instance& fs) {
  return fs.fileExists(CGROUP_V1_CPU_QUOTA) && fs.fileExists(CGROUP_V1_CPU_PERIOD);
}

} // namespace Envoy
