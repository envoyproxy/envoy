#include "extensions/resource_monitors/unused_heap/unused_heap_monitor.h"

#include "envoy/config/resource_monitor/unused_heap/v2alpha/unused_heap.pb.h"

#include "common/common/assert.h"
#include "common/memory/stats.h"

namespace Envoy {
namespace Extensions {
namespace ResourceMonitors {
namespace UnusedHeapMonitor {

UnusedHeapMonitor::UnusedHeapMonitor(
    const envoy::config::resource_monitor::unused_heap::v2alpha::UnusedHeapConfig& config,
    std::unique_ptr<Common::MemoryStatsReader> stats)
    : config_(config), stats_(std::move(stats)) {}

void UnusedHeapMonitor::updateResourceUsage(Server::ResourceMonitor::Callbacks& callbacks) {
  const size_t physical = stats_->reservedHeapBytes();
  const size_t unmapped = stats_->unmappedHeapBytes();
  ASSERT(physical >= unmapped);
  const size_t reserved = physical - unmapped;
  const size_t allocated = stats_->allocatedHeapBytes();
  ASSERT(reserved >= allocated);

  Server::ResourceUsage usage;

  switch (config_.monitor_type_case()) {
  case envoy::config::resource_monitor::unused_heap::v2alpha::UnusedHeapConfig::MonitorTypeCase::
      kMaxUnusedHeapSizeBytes: {
    usage.resource_pressure_ =
        (reserved - allocated) / static_cast<double>(config_.max_unused_heap_size_bytes());
    break;
  }
  case envoy::config::resource_monitor::unused_heap::v2alpha::UnusedHeapConfig::MonitorTypeCase::
      kMaxUnusedHeapPercent: {
    usage.resource_pressure_ = (reserved - allocated) / (reserved * 1.0) * 100.0 /
                               config_.max_unused_heap_percent().value();
    break;
  }
  default:
    NOT_REACHED_GCOVR_EXCL_LINE;
  }
  callbacks.onSuccess(usage);
}

} // namespace UnusedHeapMonitor
} // namespace ResourceMonitors
} // namespace Extensions
} // namespace Envoy
