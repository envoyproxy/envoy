#pragma once

#include "envoy/extensions/resource_monitors/fixed_heap/v3/fixed_heap.pb.h"
#include "envoy/server/resource_monitor.h"

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
};

/**
 * Heap memory monitor with a statically configured maximum.
 */
class FixedHeapMonitor : public Server::ResourceMonitor {
public:
  FixedHeapMonitor(
      const envoy::extensions::resource_monitors::fixed_heap::v3::FixedHeapConfig& config,
      std::unique_ptr<MemoryStatsReader> stats = std::make_unique<MemoryStatsReader>());

  void updateResourceUsage(Server::ResourceUpdateCallbacks& callbacks) override;

private:
  const uint64_t max_heap_;
  std::unique_ptr<MemoryStatsReader> stats_;
};

} // namespace FixedHeapMonitor
} // namespace ResourceMonitors
} // namespace Extensions
} // namespace Envoy
