#pragma once

#include "envoy/extensions/resource_monitors/fixed_heap/v3/fixed_heap.pb.h"
#include "envoy/server/resource_monitor.h"

#include "source/common/runtime/runtime_protos.h"

#include "absl/types/variant.h"

namespace Envoy {
namespace Extensions {
namespace ResourceMonitors {
namespace FixedHeapMonitor {

/**
 * Helper class for getting memory heap stats.
 */
class MemoryStatsReader {
public:
  MemoryStatsReader() = default;
  virtual ~MemoryStatsReader() = default;

  // Memory reserved for the process by the heap.
  virtual uint64_t reservedHeapBytes();
  // Memory in free, unmapped pages in the page heap.
  virtual uint64_t unmappedHeapBytes();
  // Memory in free, mapped pages in the page heap.
  virtual uint64_t freeMappedHeapBytes();
  // Memory currently allocated by the process.
  virtual uint64_t allocatedHeapBytes();
};

/**
 * Heap memory monitor with a statically configured or runtime-derived maximum.
 */
class FixedHeapMonitor : public Server::ResourceMonitor {
public:
  FixedHeapMonitor(
      absl::variant<uint64_t, Runtime::UInt64> max_heap_source,
      std::unique_ptr<MemoryStatsReader> stats = std::make_unique<MemoryStatsReader>());

  void updateResourceUsage(Server::ResourceUpdateCallbacks& callbacks) override;

private:
  const absl::variant<uint64_t, Runtime::UInt64> max_heap_source_;
  std::unique_ptr<MemoryStatsReader> stats_;
};

} // namespace FixedHeapMonitor
} // namespace ResourceMonitors
} // namespace Extensions
} // namespace Envoy
