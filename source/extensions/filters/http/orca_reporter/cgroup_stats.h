#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>

#include "envoy/filesystem/filesystem.h"

#include "source/common/matcher/regex_replace.h"

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace OrcaReporter {

// Looks up an environment variable, returning nullopt if it is not set.
using EnvLookup = std::function<std::optional<std::string>(const std::string& name)>;

// Looks up the variable in the process environment.
std::optional<std::string> processEnvLookup(const std::string& name);

// Replaces every {NAME} in the template with the value of the environment variable NAME, after
// applying the rewrites in order to that value. The literal parts of the template are kept as is.
//
// For example, with POD_UID=0f2a6b1e-41c3 and a rewrite of "-" to "_" (the systemd cgroup driver
// escapes "-" in the pod UID):
//   /host/cg/kubepods.slice/kubepods-pod{POD_UID}.slice
// resolves to:
//   /host/cg/kubepods.slice/kubepods-pod0f2a6b1e_41c3.slice
//
// An unset or empty variable, an invalid name such as {1X}, or a missing "}" is an error.
absl::StatusOr<std::string> resolvePathTemplate(absl::string_view path_template,
                                                absl::Span<const Matcher::RegexReplace> rewrites,
                                                const EnvLookup& env_lookup);

// Parsers for the cgroup v2 interface files. Each takes the full file content.

// Returns usage_usec, the cumulative CPU time in microseconds, from cpu.stat. For example:
//   usage_usec 123456789
//   user_usec 100000000
//   system_usec 23456789
// returns 123456789.
absl::StatusOr<uint64_t> parseCpuStatUsageUsec(absl::string_view cpu_stat);

// Returns the CPU limit in cores from cpu.max ("<quota> <period>", both in microseconds), or
// nullopt if there is no limit. For example:
//   "150000 100000" returns 1.5
//   "max 100000"    returns nullopt
absl::StatusOr<std::optional<double>> parseCpuMax(absl::string_view cpu_max);

// Parses a single value file such as memory.current. For example, "536870912\n" returns
// 536870912.
absl::StatusOr<uint64_t> parseSingleValue(absl::string_view content);

// Returns the memory limit in bytes from memory.max, or nullopt if there is no limit. For example:
//   "1073741824" returns 1073741824
//   "max"        returns nullopt
// A limit of 0 is an error, as it can not be used as a capacity.
absl::StatusOr<std::optional<uint64_t>> parseMemoryMax(absl::string_view memory_max);

// Returns inactive_file, the page cache that can be reclaimed, in bytes from memory.stat. For
// example:
//   anon 104857600
//   file 52428800
//   inactive_file 20971520
// returns 20971520.
absl::StatusOr<uint64_t> parseMemoryStatInactiveFile(absl::string_view memory_stat);

// Reads the interface files of a cgroup v2 directory, such as /sys/fs/cgroup.
class CgroupReader {
public:
  CgroupReader(Filesystem::Instance& fs, std::string directory)
      : fs_(fs), directory_(std::move(directory)) {}

  // Reads <directory>/cpu.stat. The value is cumulative: utilization is the difference between
  // two samples divided by the elapsed time.
  absl::StatusOr<uint64_t> cpuUsageUsec() const;
  // Reads <directory>/cpu.max.
  absl::StatusOr<std::optional<double>> cpuLimitCores() const;

  // The working set: memory.current minus inactive_file, as computed by the kubelet. For example,
  // memory.current 536870912 and inactive_file 20971520 give 515899392. It is 0, not negative, if
  // inactive_file is larger.
  absl::StatusOr<uint64_t> memoryWorkingSetBytes() const;
  // Reads <directory>/memory.max.
  absl::StatusOr<std::optional<uint64_t>> memoryLimitBytes() const;

  const std::string& directory() const { return directory_; }

private:
  absl::StatusOr<std::string> read(absl::string_view file) const;

  Filesystem::Instance& fs_;
  const std::string directory_;
};

} // namespace OrcaReporter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
