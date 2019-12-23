#pragma once

#include "envoy/config/resource_monitor/unused_heap/v2alpha/unused_heap.pb.h"
#include "envoy/server/resource_monitor.h"

#include "extensions/resource_monitors/common/memory_stats_reader.h"

namespace Envoy {
namespace Extensions {
namespace ResourceMonitors {
namespace UnusedHeapMonitor {
/**
 * Heap memory monitor with a statically configured maximum.
 */
class UnusedHeapMonitor : public Server::ResourceMonitor {
public:
  using UnusedHeapConfig = envoy::config::resource_monitor::unused_heap::v2alpha::UnusedHeapConfig;
  UnusedHeapMonitor(
      const envoy::config::resource_monitor::unused_heap::v2alpha::UnusedHeapConfig& config,
      std::unique_ptr<Common::MemoryStatsReader> stats =
          std::make_unique<Common::MemoryStatsReader>());

  void updateResourceUsage(Server::ResourceMonitor::Callbacks& callbacks) override;

private:
  const UnusedHeapConfig config_;
  std::unique_ptr<Common::MemoryStatsReader> stats_;
};

} // namespace UnusedHeapMonitor
} // namespace ResourceMonitors
} // namespace Extensions
} // namespace Envoy
