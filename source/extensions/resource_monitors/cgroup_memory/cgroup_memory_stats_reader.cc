#include "source/extensions/resource_monitors/cgroup_memory/cgroup_memory_stats_reader.h"

#include <filesystem>
#include <fstream>
#include <limits>

#include "envoy/common/exception.h"

#include "source/common/common/assert.h"
#include "source/common/common/fmt.h"

namespace Envoy {
namespace Extensions {
namespace ResourceMonitors {
namespace CgroupMemory {

uint64_t CgroupMemoryStatsReader::readMemoryStats(const std::string& path) {
  std::ifstream file(path);
  if (!file.is_open()) {
    throw EnvoyException(fmt::format("Unable to open memory stats file at {}", path));
  }

  uint64_t value;
  file >> value;
  return value;
}

std::unique_ptr<CgroupMemoryStatsReader> CgroupMemoryStatsReader::create() {
  // Try cgroup v2 first as it's the newer version
  if (CgroupPaths::isV2()) {
    return std::make_unique<CgroupV2StatsReader>();
  }

  if (CgroupPaths::isV1()) {
    return std::make_unique<CgroupV1StatsReader>();
  }

  throw EnvoyException("No supported cgroup memory implementation found");
}

// CgroupV1StatsReader implementation
uint64_t CgroupV1StatsReader::getMemoryUsage() { return readMemoryStats(usage_path_); }

uint64_t CgroupV1StatsReader::getMemoryLimit() { return readMemoryStats(limit_path_); }

// CgroupV2StatsReader implementation
uint64_t CgroupV2StatsReader::getMemoryUsage() { return readMemoryStats(usage_path_); }

uint64_t CgroupV2StatsReader::getMemoryLimit() {
  const uint64_t max_memory = readMemoryStats(limit_path_);
  // In cgroup v2, "max" file might contain "max" string instead of a number
  if (max_memory == 0) {
    return std::numeric_limits<uint64_t>::max();
  }
  return max_memory;
}

} // namespace CgroupMemory
} // namespace ResourceMonitors
} // namespace Extensions
} // namespace Envoy
