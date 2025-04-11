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

  std::string value_str;
  if (!std::getline(file, value_str)) {
    throw EnvoyException(fmt::format("Unable to read memory stats from file at {}", path));
  }

  // Trim whitespace
  value_str.erase(std::remove_if(value_str.begin(), value_str.end(), ::isspace), value_str.end());

  if (value_str.empty()) {
    throw EnvoyException(fmt::format("Empty memory stats file at {}", path));
  }

  if (value_str == "max") {
    return 0;
  }

  uint64_t value;
  TRY_ASSERT_MAIN_THREAD { value = std::stoull(value_str); }
  END_TRY
  catch (const std::exception&) {
    throw EnvoyException(fmt::format("Unable to parse memory stats from file at {}", path));
  }
  return value;
}

std::unique_ptr<CgroupMemoryStatsReader> CgroupMemoryStatsReader::create() {
  if (CgroupPaths::isV2()) {
    return std::make_unique<CgroupV2StatsReader>();
  }

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

uint64_t CgroupV2StatsReader::getMemoryLimit() {
  const uint64_t max_memory = readMemoryStats(getMemoryLimitPath());
  return max_memory == 0 ? std::numeric_limits<uint64_t>::max() : max_memory;
}

} // namespace CgroupMemory
} // namespace ResourceMonitors
} // namespace Extensions
} // namespace Envoy
