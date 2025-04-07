#pragma once

#include "envoy/extensions/resource_monitors/cgroup_memory/v3/cgroup_memory.pb.h"
#include "envoy/server/resource_monitor.h"

#include "source/extensions/resource_monitors/cgroup_memory/cgroup_memory_stats_reader.h"

namespace Envoy {
namespace Extensions {
namespace ResourceMonitors {
namespace CgroupMemory {

class CgroupMemoryMonitor : public Server::ResourceMonitor {
public:
  CgroupMemoryMonitor(
      const envoy::extensions::resource_monitors::cgroup_memory::v3::CgroupMemoryConfig& config,
      std::unique_ptr<CgroupMemoryStatsReader> stats_reader = nullptr);

  // Server::ResourceMonitor
  void updateResourceUsage(Server::ResourceUpdateCallbacks& callbacks) override;

private:
  const uint64_t max_memory_;
  std::unique_ptr<CgroupMemoryStatsReader> stats_reader_;
};

} // namespace CgroupMemory
} // namespace ResourceMonitors
} // namespace Extensions
} // namespace Envoy
