#pragma once

#include <memory>
#include <string>

#include "envoy/common/pure.h"

#include "source/extensions/resource_monitors/cgroup_memory/cgroup_memory_paths.h"

namespace Envoy {
namespace Extensions {
namespace ResourceMonitors {
namespace CgroupMemory {

/**
 * Abstract interface for reading cgroup memory statistics.
 * Supports both cgroup v1 and v2 memory subsystems.
 */
class CgroupMemoryStatsReader {
public:
  virtual ~CgroupMemoryStatsReader() = default;

  /**
   * @return Current memory usage in bytes.
   * @throw EnvoyException if stats cannot be read.
   */
  virtual uint64_t getMemoryUsage() PURE;

  /**
   * @return Memory limit in bytes.
   * @throw EnvoyException if stats cannot be read.
   */
  virtual uint64_t getMemoryLimit() PURE;

  /**
   * Factory method to create the appropriate cgroup stats reader based on system support.
   * @return Unique pointer to concrete CgroupMemoryStatsReader implementation.
   * @throw EnvoyException if no supported cgroup implementation is found.
   */
  static std::unique_ptr<CgroupMemoryStatsReader> create();

protected:
  static uint64_t readMemoryStats(const std::string& path);

  // Make path getters virtual so they can be overridden in tests
  virtual std::string getMemoryUsagePath() const = 0;
  virtual std::string getMemoryLimitPath() const = 0;
};

class CgroupV1StatsReader : public CgroupMemoryStatsReader {
public:
  uint64_t getMemoryUsage() override;
  uint64_t getMemoryLimit() override;
  std::string getMemoryUsagePath() const override { return usage_path_; }
  std::string getMemoryLimitPath() const override { return limit_path_; }

private:
  const std::string usage_path_{CgroupPaths::V1::USAGE};
  const std::string limit_path_{CgroupPaths::V1::LIMIT};
};

class CgroupV2StatsReader : public CgroupMemoryStatsReader {
public:
  uint64_t getMemoryUsage() override;
  uint64_t getMemoryLimit() override;
  std::string getMemoryUsagePath() const override { return usage_path_; }
  std::string getMemoryLimitPath() const override { return limit_path_; }

private:
  const std::string usage_path_{CgroupPaths::V2::USAGE};
  const std::string limit_path_{CgroupPaths::V2::LIMIT};
};

} // namespace CgroupMemory
} // namespace ResourceMonitors
} // namespace Extensions
} // namespace Envoy
