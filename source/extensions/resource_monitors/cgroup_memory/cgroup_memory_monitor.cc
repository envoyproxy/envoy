#include "source/extensions/resource_monitors/cgroup_memory/cgroup_memory_monitor.h"

#include "envoy/common/exception.h" // Add this include for EnvoyException

namespace Envoy {
namespace Extensions {
namespace ResourceMonitors {
namespace CgroupMemory {

CgroupMemoryMonitor::CgroupMemoryMonitor(
    const envoy::extensions::resource_monitors::cgroup_memory::v3::CgroupMemoryConfig& config,
    std::unique_ptr<CgroupMemoryStatsReader> stats_reader)
    : max_memory_(config.max_memory_bytes()),
      stats_reader_(stats_reader ? std::move(stats_reader) : CgroupMemoryStatsReader::create()) {}

void CgroupMemoryMonitor::updateResourceUsage(Server::ResourceUpdateCallbacks& callbacks) {
  TRY_ASSERT_MAIN_THREAD {
    const uint64_t usage = stats_reader_->getMemoryUsage();
    const uint64_t limit = std::min(max_memory_, stats_reader_->getMemoryLimit());

    // Ensure we don't divide by zero
    if (limit == 0) {
      callbacks.onFailure(EnvoyException("Memory limit cannot be zero"));
      return;
    }

    Server::ResourceUsage usage_stats;
    usage_stats.resource_pressure_ = static_cast<double>(usage) / limit;

    callbacks.onSuccess(usage_stats);
  }
  CATCH(const EnvoyException& error) { callbacks.onFailure(error); }
  END_TRY;
}

} // namespace CgroupMemory
} // namespace ResourceMonitors
} // namespace Extensions
} // namespace Envoy
