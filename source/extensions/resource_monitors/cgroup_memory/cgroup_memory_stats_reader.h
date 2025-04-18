#pragma once

#include <limits>
#include <memory>
#include <string>

#include "envoy/common/pure.h"

#include "cgroup_memory_paths.h"

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

  // Use a large value that's unlikely to be an actual limit.
  static constexpr uint64_t UNLIMITED_MEMORY = std::numeric_limits<uint64_t>::max();

  /**
   * @return Current memory usage in bytes.
   * @throw EnvoyException if stats cannot be read.
   */
  virtual uint64_t getMemoryUsage() PURE;

  /**
   * @return Memory limit in bytes.
   * @return UNLIMITED_MEMORY if no limit is set.
   * @throw EnvoyException if stats cannot be read.
   */
  virtual uint64_t getMemoryLimit() PURE;

  /**
   * Factory method to create the appropriate cgroup stats reader.
   * @return Unique pointer to concrete CgroupMemoryStatsReader implementation.
   * @throw EnvoyException if no supported cgroup implementation is found.
   */
  static std::unique_ptr<CgroupMemoryStatsReader> create();

protected:
  /**
   * Helper method to read and parse memory stats from cgroup files.
   * @param path Path to the memory stats file.
   * @return Memory value in bytes.
   * @throw EnvoyException if file cannot be read or parsed.
   */
  static uint64_t readMemoryStats(const std::string& path);

  /**
   * @return Path to the memory usage file.
   */
  virtual std::string getMemoryUsagePath() const PURE;

  /**
   * @return Path to the memory limit file.
   */
  virtual std::string getMemoryLimitPath() const PURE;
};

/**
 * Implementation for cgroup v1 memory subsystem.
 */
class CgroupV1StatsReader : public CgroupMemoryStatsReader {
public:
  uint64_t getMemoryUsage() override;
  uint64_t getMemoryLimit() override;

protected:
  std::string getMemoryUsagePath() const override { return CgroupPaths::V1::getUsagePath(); }
  std::string getMemoryLimitPath() const override { return CgroupPaths::V1::getLimitPath(); }
};

/**
 * Implementation for cgroup v2 memory subsystem.
 */
class CgroupV2StatsReader : public CgroupMemoryStatsReader {
public:
  uint64_t getMemoryUsage() override;
  uint64_t getMemoryLimit() override;

protected:
  std::string getMemoryUsagePath() const override { return CgroupPaths::V2::getUsagePath(); }
  std::string getMemoryLimitPath() const override { return CgroupPaths::V2::getLimitPath(); }
};

} // namespace CgroupMemory
} // namespace ResourceMonitors
} // namespace Extensions
} // namespace Envoy
