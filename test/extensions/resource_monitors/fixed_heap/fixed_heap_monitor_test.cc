#include "extensions/resource_monitors/fixed_heap/fixed_heap_monitor.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace ResourceMonitors {
namespace FixedHeapMonitor {

TEST(FixedHeapMonitorTest, SaneUsage) {
  envoy::config::resource_monitor::fixed_heap::v2alpha::FixedHeapConfig config;
  config.set_max_heap_size_bytes(std::numeric_limits<uint64_t>::max());
  std::unique_ptr<FixedHeapMonitor> monitor(new FixedHeapMonitor(config));

  Server::ResourceUsage usage;
  monitor->updateResourceUsage([&](const Server::ResourceUsage* u, const EnvoyException* error) {
    EXPECT_NE(u, nullptr);
    EXPECT_EQ(error, nullptr);
    usage = *u;
  });
  EXPECT_GE(usage.resource_pressure_, 0.0);
  EXPECT_LT(usage.resource_pressure_, 1.0);
}

} // namespace FixedHeapMonitor
} // namespace ResourceMonitors
} // namespace Extensions
} // namespace Envoy
