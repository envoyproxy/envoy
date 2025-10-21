// Container-aware CPU detection utility for Envoy
// Inspired by Go's runtime `cgroup` CPU limit detection
// See: https://github.com/golang/go/blob/go1.23.4/src/internal/cgroup/cgroup_linux.go

#include "source/server/cgroup_cpu_util.h"

#include <algorithm>
#include <cmath>

#include "source/common/common/logger.h"

#include "absl/strings/match.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_split.h"
#include "absl/strings/strip.h"

namespace Envoy {

// Implementation of CgroupDetector interface
absl::optional<uint32_t> CgroupDetectorImpl::getCpuLimit(Filesystem::Instance& fs) {
  return CgroupCpuUtil::getCpuLimit(fs);
}

// Returns the CPU limit from `cgroup` subsystem, following Go runtime behavior.
// This function prioritizes `cgroup` `v1` over `v2` when both are available,
// as `v1` CPU controllers take precedence in hybrid environments.
//
// Return values:
//   Valid uint32_t: Actual CPU limit (number of CPUs, rounded up)
//   absl::nullopt: No limit detected (unlimited CPU usage allowed)
absl::optional<uint32_t> CgroupCpuUtil::getCpuLimit(Filesystem::Instance& fs) {
  // Step 1: Mount Discovery - call once and reuse
  absl::optional<std::string> mount_opt = discoverCgroupMount(fs);
  if (!mount_opt.has_value()) {
    // No `cgroup` filesystem found
    return absl::nullopt;
  }
  const std::string& mount_point = mount_opt.value();

  // Steps 2-3: Process Assignment + Path Construction
  absl::optional<CgroupInfo> cgroup_info_opt = constructCgroupPath(mount_point, fs);
  if (!cgroup_info_opt.has_value()) {
    // No valid `cgroup` path found
    return absl::nullopt;
  }
  const CgroupInfo& cgroup_info = cgroup_info_opt.value();

  // Step 4: File Access - append version-specific filenames and validate access
  absl::optional<CpuFiles> cpu_files_opt = accessCgroupFiles(cgroup_info, fs);
  if (!cpu_files_opt.has_value()) {
    // File access failed - fallback to "no `cgroup`"
    return absl::nullopt;
  }
  const CpuFiles& cpu_files = cpu_files_opt.value();

  // Step 5: Read Actual Limits using cached file paths
  absl::optional<double> cpu_ratio = readActualLimits(cpu_files, fs);
  if (!cpu_ratio.has_value()) {
    // No valid limit found or unlimited
    return absl::nullopt;
  }

  // Convert float64 ratio to uint32_t CPU count (rounded down, minimum 1)
  const uint32_t cpu_limit = std::max(1U, static_cast<uint32_t>(std::floor(cpu_ratio.value())));
  return cpu_limit;
}

// Validates `cgroup` file content following strict requirements.
// This centralizes the validation logic used by both `v1` and `v2` `cgroup` file parsers.
//
// Validation requirements:
// - Newline requirement: Content must end with '\n'
//
// Returns string_view without trailing newline on success, absl::nullopt on validation failure.
absl::optional<absl::string_view>
CgroupCpuUtil::validateCgroupFileContent(const std::string& content, const std::string& file_path) {
  // ✅ Newline Validation: Require trailing newline
  if (content.empty() || content.back() != '\n') {
    ENVOY_LOG_MISC(warn, "Malformed `cgroup` file {}: missing trailing newline", file_path);
    return absl::nullopt;
  }

  // Return content without trailing newline
  return absl::string_view(content.data(), content.size() - 1);
}

// Parses `/proc/self/cgroup` to find the current process's `cgroup` path with priority handling.
//
// File format (one line per hierarchy):
//   `cgroup` `v2`: "0::/path/to/cgroup"
//   `cgroup` `v1`: "N:controller,list:/path/to/cgroup"
//
// Priority handling logic:
//   - If hierarchy "0": Save v2 path, continue searching
//   - If v1 hierarchy + containsCPU(): Return immediately (v1 wins)
//   - Result: Single relative path + version with highest priority
//
// Returns CgroupPathInfo with relative path and version, or absl::nullopt if no suitable `cgroup`
// found.
absl::optional<CgroupPathInfo> CgroupCpuUtil::getCurrentCgroupPath(Filesystem::Instance& fs) {
  const auto result = fs.fileReadToEnd(std::string(PROC_CGROUP_PATH));
  if (!result.ok()) {
    // `/proc/self/cgroup` doesn't exist - not in a `cgroup`
    ENVOY_LOG_MISC(warn,
                   "Cannot read `/proc/self/cgroup`: not in a `cgroup` or file doesn't exist");
    return absl::nullopt;
  }

  const std::string content = result.value();
  const std::vector<std::string> lines = absl::StrSplit(content, '\n');

  std::string v2_path;   // Save v2 path in case no v1 found
  bool found_v2 = false; // Track if we found any v2 hierarchy

  // Parse /proc/self/cgroup line by line
  for (const std::string& line : lines) {
    if (line.empty()) {
      continue;
    }

    // Extract hierarchy ID, controllers, path from hierarchy:controllers:path format
    size_t first_colon = line.find(':');
    if (first_colon == std::string::npos) {
      ENVOY_LOG_MISC(warn, "Skipping malformed cgroup line: no colon separator");
      continue;
    }

    size_t second_colon = line.find(':', first_colon + 1);
    if (second_colon == std::string::npos) {
      ENVOY_LOG_MISC(warn, "Skipping malformed cgroup line: missing second colon");
      continue;
    }

    absl::string_view hierarchy_id = absl::string_view(line).substr(0, first_colon);
    absl::string_view controllers =
        absl::string_view(line).substr(first_colon + 1, second_colon - first_colon - 1);
    absl::string_view path = absl::string_view(line).substr(second_colon + 1);

    // Priority handling: If hierarchy "0": Save v2 path, continue searching
    if (hierarchy_id == "0") {
      v2_path = std::string(path); // Save v2 path but keep searching for v1
      found_v2 = true;             // Mark that we found v2 hierarchy
      continue;
    }

    // Priority handling: If v1 hierarchy + containsCPU(): Return immediately (v1 wins)
    if (absl::StrContains(controllers, "cpu")) {
      // Found cgroup v1 with CPU controller - return immediately (highest priority)
      return CgroupPathInfo{std::string(path), "v1"};
    }
  }

  // Result: Single relative path with highest priority
  // Return v2 path if we found v2 hierarchy, or nullopt if no valid cgroup found
  if (!found_v2) {
    return absl::nullopt;
  }
  return CgroupPathInfo{v2_path, "v2"};
}

// Constructs complete cgroup path by combining mount point and process assignment.
absl::optional<CgroupInfo> CgroupCpuUtil::constructCgroupPath(const std::string& mount_point,
                                                              Filesystem::Instance& fs) {

  // Process Assignment - get relative path and determine version
  absl::optional<CgroupPathInfo> path_info_opt = getCurrentCgroupPath(fs);
  if (!path_info_opt.has_value()) {
    // No cgroup path found for this process
    return absl::nullopt;
  }
  const CgroupPathInfo& path_info = path_info_opt.value();
  const std::string& relative_path = path_info.relative_path;
  const std::string& version = path_info.version;

  // Path Construction - combine mount point and relative path
  CgroupInfo info;

  // Construct full path using absl::StrCat (efficient concatenation)
  if (!relative_path.empty() && relative_path[0] != '/') {
    info.full_path = absl::StrCat(mount_point, "/", relative_path);
  } else {
    info.full_path = absl::StrCat(mount_point, relative_path);
  }

  // Version determination from getCurrentCgroupPath
  // Version is now determined by parsing /proc/self/cgroup, not by trial and error
  info.version = version;

  ENVOY_LOG_MISC(debug, "Constructed cgroup path: {} (version: {})", info.full_path, info.version);

  // Result: Combined path in single buffer + final version
  return info;
}

// Accesses cgroup v1 CPU files (quota and period).
absl::optional<CpuFiles> CgroupCpuUtil::accessCgroupV1Files(const CgroupInfo& cgroup_info,
                                                            Filesystem::Instance& fs) {
  // Read v1 files directly - no trial and error needed
  std::string v1_quota_path = absl::StrCat(cgroup_info.full_path, CGROUP_V1_QUOTA_FILE);
  std::string v1_period_path = absl::StrCat(cgroup_info.full_path, CGROUP_V1_PERIOD_FILE);

  const auto quota_result = fs.fileReadToEnd(v1_quota_path);
  const auto period_result = fs.fileReadToEnd(v1_period_path);

  if (quota_result.ok() && period_result.ok()) {
    CpuFiles cpu_files;
    cpu_files.version = "v1";
    cpu_files.quota_content = quota_result.value();
    cpu_files.period_content = period_result.value();
    ENVOY_LOG_MISC(debug, "Using cgroup v1 files at {}", cgroup_info.full_path);
    return cpu_files;
  } else {
    // Expected v1 files don't exist - this is an error
    ENVOY_LOG_MISC(warn, "Expected cgroup v1 files not accessible at {}", cgroup_info.full_path);
    return absl::nullopt;
  }
}

// Accesses cgroup v2 CPU file (cpu.max).
absl::optional<CpuFiles> CgroupCpuUtil::accessCgroupV2Files(const CgroupInfo& cgroup_info,
                                                            Filesystem::Instance& fs) {
  // Read v2 file directly - no trial and error needed
  std::string v2_cpu_max_path = absl::StrCat(cgroup_info.full_path, CGROUP_V2_CPU_MAX_FILE);
  const auto result = fs.fileReadToEnd(v2_cpu_max_path);

  if (result.ok()) {
    CpuFiles cpu_files;
    cpu_files.version = "v2";
    cpu_files.quota_content = result.value();
    cpu_files.period_content = ""; // v2 doesn't use separate period file
    ENVOY_LOG_MISC(debug, "Using cgroup v2 file at {}", cgroup_info.full_path);
    return cpu_files;
  } else {
    // Expected v2 file doesn't exist - this is an error
    ENVOY_LOG_MISC(warn, "Expected cgroup v2 file not accessible at {}", cgroup_info.full_path);
    return absl::nullopt;
  }
}

// Accesses cgroup CPU files with version-specific filename appending and validation.
//
// Logic:
//   1. Get combined path from Step 3
//   2. Append version-specific filenames
//   3. Validate file access via filesystem interface
//   4. Error handling: File not found → return absl::nullopt
//   5. Result: CPU struct with cached file content for reading
absl::optional<CpuFiles> CgroupCpuUtil::accessCgroupFiles(const CgroupInfo& cgroup_info,
                                                          Filesystem::Instance& fs) {
  // Version is already determined by getCurrentCgroupPath() from /proc/self/cgroup parsing.
  // No need for fallback logic - we know exactly which files to read based on the version.

  if (cgroup_info.version == "v1") {
    return accessCgroupV1Files(cgroup_info, fs);
  } else if (cgroup_info.version == "v2") {
    return accessCgroupV2Files(cgroup_info, fs);
  } else {
    // Unknown version - this shouldn't happen
    ENVOY_LOG_MISC(warn, "Unknown cgroup version '{}' at {}", cgroup_info.version,
                   cgroup_info.full_path);
    return absl::nullopt;
  }
}

// Reads actual CPU limits from cgroup v1 files with quota/period parsing.
absl::optional<double> CgroupCpuUtil::readActualLimitsV1(const CpuFiles& cpu_files) {
  // v1: Use cached quota and period content (no re-reading)
  const std::string quota_str = std::string(absl::StripAsciiWhitespace(cpu_files.quota_content));
  const std::string period_str = std::string(absl::StripAsciiWhitespace(cpu_files.period_content));

  int64_t quota, period;
  if (!absl::SimpleAtoi(quota_str, &quota) || !absl::SimpleAtoi(period_str, &period)) {
    ENVOY_LOG_MISC(warn, "Failed to parse cgroup v1 values: quota='{}' period='{}'", quota_str,
                   period_str);
    return absl::nullopt;
  }

  // Handle special case: v1 quota = -1 means no limit
  if (quota == -1) {
    ENVOY_LOG_MISC(debug, "cgroup v1 unlimited CPU (quota = -1)");
    return absl::nullopt; // Unlimited - return nullopt
  }

  // Validate values
  if (period <= 0 || quota <= 0) {
    ENVOY_LOG_MISC(warn, "Invalid cgroup v1 values: quota={} period={}", quota, period);
    return absl::nullopt;
  }

  // Calculate CPU ratio as float64
  double cpu_ratio = static_cast<double>(quota) / static_cast<double>(period);

  ENVOY_LOG_MISC(debug, "cgroup v1 CPU ratio: {} (quota={}, period={})", cpu_ratio, quota, period);

  return cpu_ratio;
}

// Reads actual CPU limits from cgroup v2 files with "quota period" parsing.
absl::optional<double> CgroupCpuUtil::readActualLimitsV2(const CpuFiles& cpu_files) {
  // v2: Use cached cpu.max content (no re-reading)
  const std::string content = std::string(absl::StripAsciiWhitespace(cpu_files.quota_content));

  // Parse "quota period" format
  const std::vector<std::string> parts = absl::StrSplit(content, ' ');

  if (parts.size() != 2) {
    ENVOY_LOG_MISC(warn, "Malformed cgroup v2 cpu.max: expected 'quota period', got '{}'", content);
    return absl::nullopt;
  }

  // Handle special case: v2 quota = "max" means no limit
  if (parts[0] == "max") {
    ENVOY_LOG_MISC(debug, "cgroup v2 unlimited CPU (quota = max)");
    return absl::nullopt; // Unlimited - return nullopt
  }

  // Parse quota and period values
  uint64_t quota, period;
  if (!absl::SimpleAtoi(parts[0], &quota) || !absl::SimpleAtoi(parts[1], &period)) {
    ENVOY_LOG_MISC(warn, "Failed to parse cgroup v2 values: quota='{}' period='{}'", parts[0],
                   parts[1]);
    return absl::nullopt;
  }

  // Validate values
  if (period == 0) {
    ENVOY_LOG_MISC(warn, "Invalid cgroup v2 period: cannot be zero");
    return absl::nullopt;
  }

  // Calculate CPU ratio as float64
  double cpu_ratio = static_cast<double>(quota) / static_cast<double>(period);

  ENVOY_LOG_MISC(debug, "cgroup v2 CPU ratio: {} (quota={}, period={})", cpu_ratio, quota, period);

  return cpu_ratio;
}

// Reads actual CPU limits from cgroup files with version-specific parsing.
//
// Logic:
//   1. Use cached file paths from Step 4
//   2. Read files using filesystem interface
//   3. Version-specific parsing:
//      - v1: Read two separate files, divide quota/period
//      - v2: Parse "quota period" from single file
//   4. Handle special cases:
//      - v1: quota = -1 means no limit
//      - v2: quota = "max" means no limit
//   5. Result: CPU limit as float64 ratio
absl::optional<double> CgroupCpuUtil::readActualLimits(const CpuFiles& cpu_files,
                                                       Filesystem::Instance& /* fs */) {
  if (cpu_files.version == "v1") {
    return readActualLimitsV1(cpu_files);
  } else if (cpu_files.version == "v2") {
    return readActualLimitsV2(cpu_files);
  } else {
    ENVOY_LOG_MISC(warn, "Unknown cgroup version: {}", cpu_files.version);
    return absl::nullopt;
  }
}

// Discovers cgroup filesystem mounts by parsing /proc/self/mountinfo line by line.
// Implements proper priority handling where cgroup v1 with CPU controller wins over v2.
//
// /proc/self/mountinfo format:
// mountID parentID major:minor root mountPoint options - fsType source superOptions
// (1)     (2)      (3)        (4)  (5)       (6)     (7)(8)    (9)    (10)
//
// Priority logic:
// - If cgroup v1 + CPU controller: return immediately (highest priority)
// - If cgroup v2: save mount point, continue searching
// - Result: single mount point with highest priority
//
absl::optional<std::string> CgroupCpuUtil::discoverCgroupMount(Filesystem::Instance& fs) {
  const auto result = fs.fileReadToEnd(std::string(PROC_MOUNTINFO_PATH));
  if (!result.ok()) {
    // /proc/self/mountinfo doesn't exist - not in a cgroup
    ENVOY_LOG_MISC(warn, "Cannot read /proc/self/mountinfo: not in a cgroup or file doesn't exist");
    return absl::nullopt;
  }

  const std::string content = result.value();
  const std::vector<std::string> lines = absl::StrSplit(content, '\n');

  std::string v2_mount_point; // Save v2 mount in case no v1 found

  for (const std::string& line_str : lines) {
    if (line_str.empty()) {
      continue;
    }

    // Work with string_view for efficient parsing
    absl::string_view line = line_str;
    bool line_valid = true;

    // Skip first four fields
    for (int field = 0; field < 4; field++) {
      size_t space_pos = line.find(' ');
      if (space_pos == absl::string_view::npos) {
        ENVOY_LOG_MISC(warn, "Malformed mountinfo line: not enough fields");
        line_valid = false;
        break;
      }
      line = line.substr(space_pos + 1);
    }
    if (!line_valid)
      continue;

    // (5) mount point: extract mount point
    size_t mount_end = line.find(' ');
    if (mount_end == absl::string_view::npos) {
      ENVOY_LOG_MISC(warn, "Malformed mountinfo line: no mount point");
      continue;
    }
    absl::string_view mount_point_escaped = line.substr(0, mount_end);
    line = line.substr(mount_end + 1);

    // Skip ahead past optional fields, delimited by " - "
    bool separator_found = false;
    while (true) {
      size_t space_pos = line.find(' ');
      if (space_pos == absl::string_view::npos) {
        ENVOY_LOG_MISC(warn, "Malformed mountinfo line: no separator found");
        line_valid = false;
        break;
      }

      if (space_pos + 3 >= line.length()) {
        ENVOY_LOG_MISC(warn, "Malformed mountinfo line: separator position invalid");
        line_valid = false;
        break;
      }

      absl::string_view delim = line.substr(space_pos, 3);
      if (delim == " - ") {
        line = line.substr(space_pos + 3);
        separator_found = true;
        break;
      }
      line = line.substr(space_pos + 1);
    }
    if (!line_valid || !separator_found)
      continue;

    // (9) filesystem type: extract filesystem type
    size_t fs_type_end = line.find(' ');
    if (fs_type_end == absl::string_view::npos) {
      ENVOY_LOG_MISC(warn, "Malformed mountinfo line: no filesystem type");
      continue;
    }
    absl::string_view fs_type = line.substr(0, fs_type_end);
    line = line.substr(fs_type_end + 1);

    // Check if this is a cgroup filesystem
    if (fs_type != "cgroup" && fs_type != "cgroup2") {
      continue;
    }

    // Unescape mount point
    std::string mount_point = unescapePath(std::string(mount_point_escaped));

    // As in Go: cgroup v1 with a CPU controller takes precedence over cgroup v2
    if (fs_type == "cgroup2") {
      // v2 hierarchy - save mount point but keep searching
      v2_mount_point = mount_point;
      ENVOY_LOG_MISC(debug, "Found cgroup v2 at {}, continuing search for v1", mount_point);
      continue; // Keep searching, we might find a v1 hierarchy with CPU controller
    }

    // For cgroup v1, check for CPU controller in super options

    // (10) mount source: skip it
    size_t source_end = line.find(' ');
    if (source_end == absl::string_view::npos) {
      ENVOY_LOG_MISC(warn, "Malformed mountinfo line: no mount source");
      continue;
    }
    line = line.substr(source_end + 1);

    // (11) super options: check for CPU controller
    absl::string_view super_options = line;

    // v1 hierarchy - check for CPU controller
    if (absl::StrContains(super_options, "cpu")) {
      // Found a v1 CPU controller. This must be the only one, so we're done
      ENVOY_LOG_MISC(debug, "Found cgroup v1 with CPU controller at {}", mount_point);
      return mount_point; // Return immediately - v1 CPU wins
    }
  }

  // Return v2 mount if no v1 with CPU found
  if (!v2_mount_point.empty()) {
    ENVOY_LOG_MISC(debug, "Using cgroup v2 mount at {}", v2_mount_point);
    return v2_mount_point;
  }

  // No cgroup filesystem found
  ENVOY_LOG_MISC(debug, "No cgroup filesystem mounts found");
  return absl::nullopt;
}

// Unescapes octal escape sequences in paths from /proc/self/mountinfo.
// Linux's show_path converts '\', ' ', '\t', and '\n' to octal escape sequences
// like '\040' for space, '\134' for backslash, '\011' for tab, '\012' for newline.
//
// This matches the Go runtime implementation:
// https://github.com/golang/go/blob/master/src/internal/runtime/cgroup/cgroup_linux.go
std::string CgroupCpuUtil::unescapePath(const std::string& path) {
  std::string result;
  result.reserve(path.length()); // Pre-allocate to avoid `reallocations`

  for (size_t i = 0; i < path.length(); ++i) {
    char c = path[i];

    // Check for escape sequence start
    if (c != '\\') {
      result += c;
      continue;
    }

    // Start of escape sequence: backslash followed by 3 octal digits
    // Escape sequence is always 4 characters: one backslash and three digits
    if (i + 3 >= path.length()) {
      // Invalid escape sequence - not enough characters
      ENVOY_LOG_MISC(warn, "Invalid escape sequence in path '{}' at position {}", path, i);
      result += c; // Keep the backslash as-is
      continue;
    }

    // Parse three octal digits using `std::strtol`
    // Extract exactly 3 characters after the backslash
    if (i + 3 >= path.length()) {
      // Not enough characters for complete octal sequence
      ENVOY_LOG_MISC(warn, "Incomplete octal escape sequence in path '{}' at position {}", path, i);
      result += c; // Keep the backslash as-is
      continue;
    }

    std::string octal_str = path.substr(i + 1, 3);

    // Validate all characters are valid octal digits (0-7)
    bool valid = std::all_of(octal_str.begin(), octal_str.end(),
                             [](char c) { return c >= '0' && c <= '7'; });

    if (!valid) {
      // Invalid octal digits found
      ENVOY_LOG_MISC(warn, "Invalid octal escape sequence in path '{}' at position {}", path, i);
      result += c; // Keep the backslash as-is
      continue;
    }

    // Convert octal string to integer
    char* end;
    long decoded = std::strtol(octal_str.c_str(), &end, 8);

    // Verify conversion was successful and complete
    if (end != octal_str.c_str() + 3 || decoded > 255) {
      ENVOY_LOG_MISC(warn, "Invalid octal escape sequence in path '{}' at position {}", path, i);
      result += c; // Keep the backslash as-is
      continue;
    }

    // Valid escape sequence - add decoded character
    result += static_cast<char>(decoded);
    i += 3; // Skip the three digits (loop will increment i by 1)
  }

  return result;
}

// Parses a single line from /proc/self/mountinfo to extract cgroup mount point.
// Format: mountID parentID major:minor root mountPoint options - fsType source superOptions
//
// Example lines:
// 25 21 0:21 / /sys/fs/cgroup/cpu rw,`relatime` - cgroup cgroup rw,cpu
// 26 21 0:22 / /sys/fs/cgroup cgroup2 rw,`relatime` - cgroup2 cgroup2 rw
//
// We extract field 5 (mount point) for cgroup/cgroup2 filesystem only.
//
// NOTE: Mount points may contain escaped characters (\040 for space, \134 for backslash, etc.)
// and must be unescaped before use.
absl::optional<std::string> CgroupCpuUtil::parseMountInfoLine(const std::string& line) {
  const std::vector<std::string> fields = absl::StrSplit(line, ' ');

  // Find the separator "-" to locate filesystem type field
  size_t separator_pos = 0;
  for (size_t i = 0; i < fields.size(); i++) {
    if (fields[i] == "-") {
      separator_pos = i;
      break;
    }
  }

  if (separator_pos == 0 || separator_pos + 1 >= fields.size()) {
    // Malformed line or separator not found
    ENVOY_LOG_MISC(warn, "Malformed mountinfo line: separator '-' not found or invalid position");
    return absl::nullopt;
  }

  // Extract mount point (field 5, 0-indexed = 4) and filesystem type (separator + 1)
  if (fields.size() < 5 || separator_pos + 1 >= fields.size()) {
    // Insufficient fields
    ENVOY_LOG_MISC(warn,
                   "Malformed mountinfo line: expected at least 5 fields and filesystem type after "
                   "separator, got {} fields",
                   fields.size());
    return absl::nullopt;
  }

  const std::string& mount_point_escaped = fields[4];
  const std::string& fs_type = fields[separator_pos + 1];

  // Check if this is a cgroup filesystem
  if (fs_type != "cgroup" && fs_type != "cgroup2") {
    return absl::nullopt;
  }

  // Unescape mount point - Linux's show_path escapes special characters
  std::string mount_point = unescapePath(mount_point_escaped);

  ENVOY_LOG_MISC(trace, "Parsed cgroup mount: {} ({})", mount_point, fs_type);

  return mount_point;
}

} // namespace Envoy
