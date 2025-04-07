#pragma once

#include <memory>
#include <string>

#include "envoy/common/pure.h"

#include "source/extensions/resource_monitors/cgroup_memory/cgroup_memory_paths.h"

namespace Envoy {
namespace Extensions {
namespace ResourceMonitors {
namespace CgroupMemory {

class CgroupMemoryStatsReader {
public:
  virtual ~CgroupMemoryStatsReader() = default;

  // Returns current memory usage in bytes
  virtual uint64_t getMemoryUsage() PURE;
  // Returns memory limit in bytes
  virtual uint64_t getMemoryLimit() PURE;

  // Factory method to create the appropriate cgroup stats reader
  static std::unique_ptr<CgroupMemoryStatsReader> create();

protected:
  static uint64_t readMemoryStats(const std::string& path);
};

class CgroupV1StatsReader : public CgroupMemoryStatsReader {
public:
  uint64_t getMemoryUsage() override;
  uint64_t getMemoryLimit() override;

private:
  const std::string usage_path_{CgroupPaths::V1::USAGE};
  const std::string limit_path_{CgroupPaths::V1::LIMIT};
};

class CgroupV2StatsReader : public CgroupMemoryStatsReader {
public:
  uint64_t getMemoryUsage() override;
  uint64_t getMemoryLimit() override;

private:
  const std::string usage_path_{CgroupPaths::V2::USAGE};
  const std::string limit_path_{CgroupPaths::V2::LIMIT};
};

} // namespace CgroupMemory
} // namespace ResourceMonitors
} // namespace Extensions
} // namespace Envoy
