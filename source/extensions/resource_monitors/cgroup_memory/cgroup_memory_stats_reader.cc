#include "source/extensions/resource_monitors/cgroup_memory/cgroup_memory_stats_reader.h"

#include "source/common/common/fmt.h"

#include "absl/strings/ascii.h"
#include "absl/strings/numbers.h"

namespace Envoy {
namespace Extensions {
namespace ResourceMonitors {
namespace CgroupMemory {

absl::StatusOr<uint64_t> CgroupMemoryStatsReader::readMemoryStats(const std::string& path) {
  auto result = fs_.fileReadToEnd(path);
  if (!result.ok()) {
    return absl::InvalidArgumentError(fmt::format("Unable to read memory stats file at {}", path));
  }

  // Strip whitespace in place and get a reference to the modified string.
  absl::StripAsciiWhitespace(&result.value());
  const std::string& value_str = result.value();

  if (value_str.empty()) {
    return absl::InvalidArgumentError(fmt::format("Empty memory stats file at {}", path));
  }

  // Handle cgroup v2 "max" and cgroup v1 "-1" formats for unlimited.
  if (value_str == "max" || value_str == "-1") {
    return UNLIMITED_MEMORY;
  }

  uint64_t value;
  if (!absl::SimpleAtoi(value_str, &value)) {
    return absl::InvalidArgumentError(
        fmt::format("Unable to parse memory stats from file at {}", path));
  }

  return value;
}

absl::StatusOr<CgroupMemoryStatsReader::StatsReaderPtr>
CgroupMemoryStatsReader::create(Filesystem::Instance& fs) {
  // Check if host supports cgroup v2.
  if (CgroupPaths::isV2(fs)) {
    return std::make_unique<CgroupV2StatsReader>(fs);
  }

  // Check if host supports cgroup v1.
  if (CgroupPaths::isV1(fs)) {
    return std::make_unique<CgroupV1StatsReader>(fs);
  }

  return absl::InvalidArgumentError("No supported cgroup memory implementation found");
}

} // namespace CgroupMemory
} // namespace ResourceMonitors
} // namespace Extensions
} // namespace Envoy
