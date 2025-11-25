#pragma once

#include <memory>
#include <string>

#include "envoy/common/time.h"
#include "envoy/filesystem/filesystem.h"

#include "source/common/common/logger.h"
#include "source/extensions/resource_monitors/cpu_utilization/cpu_paths.h"
#include "source/extensions/resource_monitors/cpu_utilization/cpu_stats_reader.h"

namespace Envoy {
namespace Extensions {
namespace ResourceMonitors {
namespace CpuUtilizationMonitor {

static const std::string LINUX_CPU_STATS_FILE = "/proc/stat";

/**
 * Reads CPU stats from /proc/stat for host-level CPU monitoring.
 */
class LinuxCpuStatsReader : public CpuStatsReader {
public:
  explicit LinuxCpuStatsReader(const std::string& cpu_stats_filename = LINUX_CPU_STATS_FILE);
  CpuTimes getCpuTimes() override;

private:
  const std::string cpu_stats_filename_;
};

/**
 * Base class for container CPU stats readers which supports 
 * both cgroup v1 and v2 implementations.
 */
class LinuxContainerCpuStatsReader : public CpuStatsReader {
public:
  using ContainerStatsReaderPtr = std::unique_ptr<LinuxContainerCpuStatsReader>;

  virtual ~LinuxContainerCpuStatsReader() = default;

  /**
   * Create the appropriate cgroup stats reader.
   * @param fs Filesystem instance to use for file operations.
   * @param time_source TimeSource for measuring elapsed time.
   * @return Unique pointer to concrete LinuxContainerCpuStatsReader implementation.
   * @throw EnvoyException if no supported cgroup implementation is found.
   */
  static ContainerStatsReaderPtr create(Filesystem::Instance& fs, TimeSource& time_source);

protected:
  LinuxContainerCpuStatsReader(Filesystem::Instance& fs, TimeSource& time_source)
      : fs_(fs), time_source_(time_source) {}

  Filesystem::Instance& fs_;
  TimeSource& time_source_;
};


class CgroupV1CpuStatsReader : public LinuxContainerCpuStatsReader,
                               private Logger::Loggable<Logger::Id::main> {
public:
  explicit CgroupV1CpuStatsReader(Filesystem::Instance& fs, TimeSource& time_source);

  // Test-friendly constructor that accepts custom file paths
  CgroupV1CpuStatsReader(Filesystem::Instance& fs, TimeSource& time_source,
                         const std::string& shares_path, const std::string& usage_path);

  CpuTimes getCpuTimes() override;

private:
  static constexpr double CONTAINER_MILLICORES_PER_CORE = 1000.0;
  std::string shares_path_;
  std::string usage_path_;
};


class CgroupV2CpuStatsReader : public LinuxContainerCpuStatsReader,
                               private Logger::Loggable<Logger::Id::main> {
public:
  explicit CgroupV2CpuStatsReader(Filesystem::Instance& fs, TimeSource& time_source);

  // Test-friendly constructor that accepts custom file paths
  CgroupV2CpuStatsReader(Filesystem::Instance& fs, TimeSource& time_source,
                         const std::string& stat_path, const std::string& max_path,
                         const std::string& effective_path);

  CpuTimes getCpuTimes() override;

private:
  std::string stat_path_;
  std::string max_path_;
  std::string effective_path_;
};

} // namespace CpuUtilizationMonitor
} // namespace ResourceMonitors
} // namespace Extensions
} // namespace Envoy
