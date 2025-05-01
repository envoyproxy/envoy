#include "source/extensions/resource_monitors/cgroup_memory/cgroup_memory_stats_reader.h"

#include "envoy/common/exception.h"

#include "source/common/common/assert.h"
#include "source/common/common/fmt.h"
#include "source/common/common/thread.h"

#include "absl/strings/ascii.h"
#include "absl/strings/str_cat.h"

namespace Envoy {
namespace Extensions {
namespace ResourceMonitors {
namespace CgroupMemory {

uint64_t CgroupMemoryStatsReader::readMemoryStats(const std::string& path) {
  auto result = fs_.fileReadToEnd(path);
  if (!result.ok()) {
    throw EnvoyException(fmt::format("Unable to read memory stats file at {}", path));
  }

  // Strip whitespace in place and get a reference to the modified string
  absl::StripAsciiWhitespace(&result.value());
  const std::string& value_str = result.value();

  if (value_str.empty()) {
    throw EnvoyException(fmt::format("Empty memory stats file at {}", path));
  }

  // Handle cgroup v2 "max" format for unlimited
  if (value_str == "max") {
    return UNLIMITED_MEMORY;
  }

  uint64_t value;
  TRY_ASSERT_MAIN_THREAD {
    value = std::stoull(value_str);
    // Handle cgroup v1 "-1" format for unlimited
    if (value == std::numeric_limits<uint64_t>::max()) {
      return UNLIMITED_MEMORY;
    }
  }
  END_TRY
  catch (const std::exception&) {
    throw EnvoyException(fmt::format("Unable to parse memory stats from file at {}", path));
  }
  return value;
}

CgroupMemoryStatsReader::StatsReaderPtr CgroupMemoryStatsReader::create(Filesystem::Instance& fs) {
  // Check if host supports cgroup v2
  if (CgroupPaths::isV2(fs)) {
    return std::make_unique<CgroupV2StatsReader>(fs);
  }

  // Check if host supports cgroup v1
  if (CgroupPaths::isV1(fs)) {
    return std::make_unique<CgroupV1StatsReader>(fs);
  }

  throw EnvoyException("No supported cgroup memory implementation found");
}

} // namespace CgroupMemory
} // namespace ResourceMonitors
} // namespace Extensions
} // namespace Envoy
