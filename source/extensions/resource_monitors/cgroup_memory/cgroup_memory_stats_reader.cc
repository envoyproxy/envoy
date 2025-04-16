#include "source/extensions/resource_monitors/cgroup_memory/cgroup_memory_stats_reader.h"

#include <filesystem>
#include <fstream>
#include <limits>

#include "envoy/common/exception.h"

#include "source/common/common/assert.h"
#include "source/common/common/fmt.h"
#include "source/common/common/thread.h"

namespace Envoy {
namespace Extensions {
namespace ResourceMonitors {
namespace CgroupMemory {

uint64_t CgroupMemoryStatsReader::readMemoryStats(const std::string& path) {
  std::ifstream file(path);
  if (!file.is_open()) {
    throw EnvoyException(fmt::format("Unable to open memory stats file at {}", path));
  }
  // Memory usage and limit files in both cgroup v1 and v2 contain single-line values
  std::string value_str;
  if (!std::getline(file, value_str)) {
    throw EnvoyException(fmt::format("Unable to read memory stats from file at {}", path));
  }

  // Trim whitespace
  value_str.erase(std::remove_if(value_str.begin(), value_str.end(), ::isspace), value_str.end());

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

std::unique_ptr<CgroupMemoryStatsReader> CgroupMemoryStatsReader::create() {
  // Check if host supports cgroup v2
  if (CgroupPaths::isV2()) {
    return std::make_unique<CgroupV2StatsReader>();
  }

  // Check if host supports cgroup v1
  if (CgroupPaths::isV1()) {
    return std::make_unique<CgroupV1StatsReader>();
  }

  throw EnvoyException("No supported cgroup memory implementation found");
}

// CgroupV1StatsReader implementation
uint64_t CgroupV1StatsReader::getMemoryUsage() { return readMemoryStats(getMemoryUsagePath()); }

uint64_t CgroupV1StatsReader::getMemoryLimit() { return readMemoryStats(getMemoryLimitPath()); }

// CgroupV2StatsReader implementation
uint64_t CgroupV2StatsReader::getMemoryUsage() { return readMemoryStats(getMemoryUsagePath()); }

uint64_t CgroupV2StatsReader::getMemoryLimit() { return readMemoryStats(getMemoryLimitPath()); }

} // namespace CgroupMemory
} // namespace ResourceMonitors
} // namespace Extensions
} // namespace Envoy
