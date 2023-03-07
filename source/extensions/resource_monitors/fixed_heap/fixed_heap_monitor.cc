#include "source/extensions/resource_monitors/fixed_heap/fixed_heap_monitor.h"

#include "envoy/extensions/resource_monitors/fixed_heap/v3/fixed_heap.pb.h"

#include "source/common/common/assert.h"
#include "source/common/memory/stats.h"
#include "source/common/runtime/runtime_features.h"

namespace Envoy {
namespace Extensions {
namespace ResourceMonitors {
namespace FixedHeapMonitor {

uint64_t MemoryStatsReader::reservedHeapBytes() { return Memory::Stats::totalCurrentlyReserved(); }

uint64_t MemoryStatsReader::unmappedHeapBytes() { return Memory::Stats::totalPageHeapUnmapped(); }

uint64_t MemoryStatsReader::freeMappedHeapBytes() { return Memory::Stats::totalPageHeapFree(); }

FixedHeapMonitor::FixedHeapMonitor(
    const envoy::extensions::resource_monitors::fixed_heap::v3::FixedHeapConfig& config,
    std::unique_ptr<MemoryStatsReader> stats)
    : max_heap_(config.max_heap_size_bytes()), stats_(std::move(stats)) {
  ASSERT(max_heap_ > 0);
}

void FixedHeapMonitor::updateResourceUsage(Server::ResourceUpdateCallbacks& callbacks) {

  auto computeUsedMemory = [this]() -> size_t {
    if (!Runtime::runtimeFeatureEnabled(
            "envoy.reloadable_features.do_not_count_mapped_pages_as_free")) {
      const size_t physical = stats_->reservedHeapBytes();
      const size_t unmapped = stats_->unmappedHeapBytes();
      const size_t free_mapped = stats_->freeMappedHeapBytes();
      ASSERT(physical >= (unmapped + free_mapped));
      return physical - unmapped - free_mapped;
    } else {
      const size_t physical = stats_->reservedHeapBytes();
      const size_t unmapped = stats_->unmappedHeapBytes();
      ASSERT(physical >= unmapped);
      return physical - unmapped;
    }
  };

  const size_t used = computeUsedMemory();

  Server::ResourceUsage usage;
  usage.resource_pressure_ = used / static_cast<double>(max_heap_);

  ENVOY_LOG_MISC(trace, "FixedHeapMonitor: used={}, max_heap={}, pressure={}", used, max_heap_,
                 usage.resource_pressure_);

  callbacks.onSuccess(usage);
}

} // namespace FixedHeapMonitor
} // namespace ResourceMonitors
} // namespace Extensions
} // namespace Envoy
