#include "source/extensions/resource_monitors/fixed_heap/fixed_heap_monitor.h"

#include "envoy/extensions/resource_monitors/fixed_heap/v3/fixed_heap.pb.h"

#include "source/common/common/assert.h"
#include "source/common/memory/stats.h"

namespace Envoy {
namespace Extensions {
namespace ResourceMonitors {
namespace FixedHeapMonitor {

uint64_t MemoryStatsReader::reservedHeapBytes() { return Memory::Stats::totalCurrentlyReserved(); }

uint64_t MemoryStatsReader::unmappedHeapBytes() { return Memory::Stats::totalPageHeapUnmapped(); }

FixedHeapMonitor::FixedHeapMonitor(
    const envoy::extensions::resource_monitors::fixed_heap::v3::FixedHeapConfig& config,
    std::unique_ptr<MemoryStatsReader> stats)
    : max_heap_(config.max_heap_size_bytes()), stats_(std::move(stats)) {
  ASSERT(max_heap_ > 0);
}

void FixedHeapMonitor::updateResourceUsage(Server::ResourceUpdateCallbacks& callbacks) {
  const size_t physical = stats_->reservedHeapBytes();
  const size_t unmapped = stats_->unmappedHeapBytes();
  ASSERT(physical >= unmapped);
  const size_t used = physical - unmapped;

  Server::ResourceUsage usage;
  usage.resource_pressure_ = used / static_cast<double>(max_heap_);

  callbacks.onSuccess(usage);
}

} // namespace FixedHeapMonitor
} // namespace ResourceMonitors
} // namespace Extensions
} // namespace Envoy
