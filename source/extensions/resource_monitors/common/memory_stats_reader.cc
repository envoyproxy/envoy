#include "extensions/resource_monitors/common/memory_stats_reader.h"

#include <cstdint>

#include "common/memory/stats.h"

namespace Envoy {
namespace Extensions {
namespace ResourceMonitors {
namespace Common {

uint64_t MemoryStatsReader::reservedHeapBytes() { return Memory::Stats::totalCurrentlyReserved(); }

uint64_t MemoryStatsReader::unmappedHeapBytes() { return Memory::Stats::totalPageHeapUnmapped(); }

uint64_t MemoryStatsReader::allocatedHeapBytes() {
  return Memory::Stats::totalCurrentlyAllocated();
}

} // namespace Common
} // namespace ResourceMonitors
} // namespace Extensions
} // namespace Envoy