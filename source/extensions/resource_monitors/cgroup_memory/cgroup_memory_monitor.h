#pragma once

#include "envoy/extensions/resource_monitors/cgroup_memory/v3/cgroup_memory.pb.h"
#include "envoy/server/resource_monitor.h"

#include "source/extensions/resource_monitors/cgroup_memory/cgroup_memory_stats_reader.h"

namespace Envoy {
namespace Extensions {
namespace ResourceMonitors {
namespace CgroupMemory {

/**
 * Resource monitor implementation using cgroup memory statistics.
 */
class CgroupMemoryMonitor : public Server::ResourceMonitor {
public:
  using StatsReaderPtr = std::unique_ptr<CgroupMemoryStatsReader>;

  /**
   * Creates a new monitor with the given configuration.
   * @param config Configuration for the monitor.
   * @param stats_reader Reader for cgroup memory statistics.
   */
  CgroupMemoryMonitor(
      const envoy::extensions::resource_monitors::cgroup_memory::v3::CgroupMemoryConfig& config,
      StatsReaderPtr stats_reader);

  /**
   * Updates resource pressure based on current memory usage.
   * @param callbacks Callbacks to report resource pressure or errors.
   */
  void updateResourceUsage(Server::ResourceUpdateCallbacks& callbacks) override;

private:
  // Maximum memory limit in bytes.
  const uint64_t max_memory_bytes_;
  // Reader for cgroup memory statistics.
  StatsReaderPtr stats_reader_;
};

} // namespace CgroupMemory
} // namespace ResourceMonitors
} // namespace Extensions
} // namespace Envoy
