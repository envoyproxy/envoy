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
    : max_memory_(config.max_memory_bytes()),
      stats_reader_(stats_reader ? std::move(stats_reader) : CgroupMemoryStatsReader::create()) {}

void CgroupMemoryMonitor::updateResourceUsage(Server::ResourceUpdateCallbacks& callbacks) {
  TRY_ASSERT_MAIN_THREAD {
    const uint64_t usage = stats_reader_->getMemoryUsage();
    const uint64_t limit = std::min(max_memory_, stats_reader_->getMemoryLimit());

    if (limit == 0) {
      throw EnvoyException("Memory limit cannot be zero");
    }

    if (usage > std::numeric_limits<double>::max()) {
      throw EnvoyException("Memory usage value too large");
    }

    Server::ResourceUsage usage_stats;
    usage_stats.resource_pressure_ = static_cast<double>(usage) / limit;

    callbacks.onSuccess(usage_stats);
  }
  END_TRY
  catch (const EnvoyException& error) {
    callbacks.onFailure(error);
  }
}

} // namespace CgroupMemory
} // namespace ResourceMonitors
} // namespace Extensions
} // namespace Envoy
