#pragma once
#include <cstdint>

namespace Envoy {
namespace Extensions {
namespace ResourceMonitors {
namespace Common {

class MemoryStatsReader {
public:
  MemoryStatsReader() = default;
  virtual ~MemoryStatsReader() = default;

  // Bytes of system memory reserved by process, but not necessarily allocated.
  virtual uint64_t reservedHeapBytes();
  // Memory in free, unmapped pages in the page heap.
  virtual uint64_t unmappedHeapBytes();

  // Number of bytes used by the application
  virtual uint64_t allocatedHeapBytes();
};

} // namespace Common
} // namespace ResourceMonitors
} // namespace Extensions
} // namespace Envoy