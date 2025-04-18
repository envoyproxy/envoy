#include "source/extensions/resource_monitors/cgroup_memory/cgroup_memory_monitor.h"

#include "envoy/common/exception.h"

#include "source/common/common/assert.h"
#include "source/common/common/thread.h"

namespace Envoy {
namespace Extensions {
namespace ResourceMonitors {
namespace CgroupMemory {

CgroupMemoryMonitor::CgroupMemoryMonitor(
    const envoy::extensions::resource_monitors::cgroup_memory::v3::CgroupMemoryConfig& config,
    std::unique_ptr<CgroupMemoryStatsReader> stats_reader)
    : max_memory_bytes_(config.has_max_memory_bytes() ? config.max_memory_bytes().value() : 0),
      stats_reader_(stats_reader ? std::move(stats_reader) : CgroupMemoryStatsReader::create()) {}

void CgroupMemoryMonitor::updateResourceUsage(Server::ResourceUpdateCallbacks& callbacks) {
  uint64_t usage;
  uint64_t raw_limit;

  TRY_ASSERT_MAIN_THREAD {
    usage = stats_reader_->getMemoryUsage();
    raw_limit = stats_reader_->getMemoryLimit();
  }
  END_TRY
  catch (const EnvoyException& e) {
    callbacks.onFailure(e);
    return;
  }

  Server::ResourceUsage usage_stats;

  // Use config limit if set, otherwise use cgroup limit
  const uint64_t limit = max_memory_bytes_ > 0 ? max_memory_bytes_ : raw_limit;

  if (limit == CgroupMemoryStatsReader::UNLIMITED_MEMORY) {
    // When memory is unlimited, there is no memory pressure
    usage_stats.resource_pressure_ = 0.0;
    callbacks.onSuccess(usage_stats);
    return;
  }

  // Calculate memory pressure as a percentage of the limit
  // No need to check for zero since limit is guaranteed to be non-zero
  usage_stats.resource_pressure_ = static_cast<double>(usage) / limit;
  callbacks.onSuccess(usage_stats);
}

} // namespace CgroupMemory
} // namespace ResourceMonitors
} // namespace Extensions
} // namespace Envoy
