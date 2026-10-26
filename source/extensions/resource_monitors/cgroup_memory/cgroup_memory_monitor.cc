#include "source/extensions/resource_monitors/cgroup_memory/cgroup_memory_monitor.h"

namespace Envoy {
namespace Extensions {
namespace ResourceMonitors {
namespace CgroupMemory {

CgroupMemoryMonitor::CgroupMemoryMonitor(
    const envoy::extensions::resource_monitors::cgroup_memory::v3::CgroupMemoryConfig& config,
    StatsReaderPtr stats_reader)
    : max_memory_bytes_(config.max_memory_bytes()), stats_reader_(std::move(stats_reader)) {}

void CgroupMemoryMonitor::updateResourceUsage(Server::ResourceUpdateCallbacks& callbacks) {
  auto usage_or = stats_reader_->getMemoryUsage();
  if (!usage_or.ok()) {
    callbacks.onFailure(usage_or.status());
    return;
  }

  auto limit_or = stats_reader_->getMemoryLimit();
  if (!limit_or.ok()) {
    callbacks.onFailure(limit_or.status());
    return;
  }

  const uint64_t usage = usage_or.value();
  const uint64_t raw_limit = limit_or.value();

  Server::ResourceUsage usage_stats;

  // Use cgroup limit if config limit is not set (0), otherwise use min(config limit, cgroup limit).
  const uint64_t limit = max_memory_bytes_ > 0 ? std::min(max_memory_bytes_, raw_limit) : raw_limit;

  if (limit == CgroupMemoryStatsReader::UNLIMITED_MEMORY) {
    // When memory is unlimited, there is no memory pressure.
    usage_stats.resource_pressure_ = 0.0;
    callbacks.onSuccess(usage_stats);
    return;
  }

  // Calculate memory pressure as a percentage of the limit.
  usage_stats.resource_pressure_ = static_cast<double>(usage) / limit;
  callbacks.onSuccess(usage_stats);
}

} // namespace CgroupMemory
} // namespace ResourceMonitors
} // namespace Extensions
} // namespace Envoy
