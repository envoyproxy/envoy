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
    Filesystem::Instance& fs)
    : max_memory_bytes_(config.max_memory_bytes()), fs_(fs),
      stats_reader_(CgroupMemoryStatsReader::create(fs_)) {}

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

  // Use cgroup limit if config limit is not set (0), otherwise use min(config limit, cgroup limit)
  const uint64_t limit = max_memory_bytes_ > 0 ? std::min(max_memory_bytes_, raw_limit) : raw_limit;

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
