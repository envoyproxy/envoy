#include "envoy/extensions/resource_monitors/downstream_connections/v3/downstream_connections.pb.h"

#include "source/extensions/resource_monitors/downstream_connections/downstream_connections_monitor.h"

#include "absl/types/optional.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace ResourceMonitors {
namespace DownstreamConnections {
namespace {

TEST(ActiveDownstreamConnectionsMonitorTest, CannotAllocateDeallocateResourceWithDefaultConfig) {
  envoy::extensions::resource_monitors::downstream_connections::v3::DownstreamConnectionsConfig
      config;
  std::unique_ptr<ActiveDownstreamConnectionsResourceMonitor> monitor(
      new ActiveDownstreamConnectionsResourceMonitor(config));
  EXPECT_FALSE(monitor->tryAllocateResource(1));
  EXPECT_DEATH(monitor->tryDeallocateResource(1),
               ".*Cannot deallocate resource, current resource usage is lower than decrement.*");
  EXPECT_EQ(0, monitor->currentResourceUsage());
}

TEST(ActiveDownstreamConnectionsMonitorTest, ComputesCorrectUsage) {
  envoy::extensions::resource_monitors::downstream_connections::v3::DownstreamConnectionsConfig
      config;
  config.set_max_active_downstream_connections(10);
  std::unique_ptr<ActiveDownstreamConnectionsResourceMonitor> monitor(
      new ActiveDownstreamConnectionsResourceMonitor(config));
  EXPECT_EQ(0, monitor->currentResourceUsage());
  EXPECT_EQ(10, monitor->maxResourceUsage());
  EXPECT_TRUE(monitor->tryAllocateResource(3));
  EXPECT_EQ(3, monitor->currentResourceUsage());
  EXPECT_TRUE(monitor->tryDeallocateResource(2));
  EXPECT_EQ(1, monitor->currentResourceUsage());
}

TEST(ActiveDownstreamConnectionsMonitorTest, FailsToAllocateDeallocateWhenMinMaxHit) {
  envoy::extensions::resource_monitors::downstream_connections::v3::DownstreamConnectionsConfig
      config;
  config.set_max_active_downstream_connections(1);
  std::unique_ptr<ActiveDownstreamConnectionsResourceMonitor> monitor(
      new ActiveDownstreamConnectionsResourceMonitor(config));
  EXPECT_EQ(0, monitor->currentResourceUsage());
  EXPECT_EQ(1, monitor->maxResourceUsage());
  EXPECT_TRUE(monitor->tryAllocateResource(1));
  EXPECT_FALSE(monitor->tryAllocateResource(1));
  EXPECT_TRUE(monitor->tryDeallocateResource(1));
  EXPECT_DEATH(monitor->tryDeallocateResource(1),
               ".*Cannot deallocate resource, current resource usage is lower than decrement.*");
  EXPECT_EQ(0, monitor->currentResourceUsage());
}

} // namespace
} // namespace DownstreamConnections
} // namespace ResourceMonitors
} // namespace Extensions
} // namespace Envoy
