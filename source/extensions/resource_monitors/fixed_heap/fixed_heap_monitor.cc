#include "extensions/resource_monitors/fixed_heap/fixed_heap_monitor.h"

#include "common/common/assert.h"
#include "common/memory/stats.h"

namespace Envoy {
namespace Extensions {
namespace ResourceMonitors {
namespace FixedHeapMonitor {

FixedHeapMonitor::FixedHeapMonitor(
    const envoy::config::resource_monitor::fixed_heap::v2alpha::FixedHeapConfig& config)
    : max_heap_(config.max_heap_size_bytes()) {
  ASSERT(max_heap_ > 0);
}

void FixedHeapMonitor::updateResourceUsage(const Server::ResourceMonitor::UpdateCb& completionCb) {
  size_t physical = Memory::Stats::totalCurrentlyReserved();
  size_t unmapped = Memory::Stats::totalPageHeapUnmapped();
  size_t used = std::max<size_t>(physical - unmapped, 0);

  Server::ResourceUsage usage;
  usage.resource_pressure_ = used / static_cast<double>(max_heap_);

  completionCb(&usage, nullptr);
}

} // namespace FixedHeapMonitor
} // namespace ResourceMonitors
} // namespace Extensions
} // namespace Envoy
