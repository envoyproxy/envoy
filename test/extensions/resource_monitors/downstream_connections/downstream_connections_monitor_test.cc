#include "envoy/extensions/resource_monitors/downstream_connections/v3/downstream_connections.pb.h"

#include "source/extensions/resource_monitors/downstream_connections/downstream_connections_monitor.h"

#include "absl/types/optional.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace ResourceMonitors {
namespace DownstreamConnections {

class ActiveDownstreamConnectionsMonitorTest : public testing::Test {
public:
  void initialize(const envoy::extensions::resource_monitors::downstream_connections::v3::
                      DownstreamConnectionsConfig& config) {
    monitor_ = std::make_unique<ActiveDownstreamConnectionsResourceMonitor>(config);
  }

  Thread::ThreadSynchronizer& synchronizer() { return monitor_->synchronizer_; }

  std::unique_ptr<ActiveDownstreamConnectionsResourceMonitor> monitor_;
};

TEST_F(ActiveDownstreamConnectionsMonitorTest, CannotAllocateDeallocateResourceWithDefaultConfig) {
  envoy::extensions::resource_monitors::downstream_connections::v3::DownstreamConnectionsConfig
      config;
  initialize(config);
  EXPECT_FALSE(monitor_->tryAllocateResource(1));
  EXPECT_EQ(0, monitor_->currentResourceUsage());
}

TEST_F(ActiveDownstreamConnectionsMonitorTest, ComputesCorrectUsage) {
  envoy::extensions::resource_monitors::downstream_connections::v3::DownstreamConnectionsConfig
      config;
  config.set_max_active_downstream_connections(10);
  initialize(config);
  EXPECT_EQ(0, monitor_->currentResourceUsage());
  EXPECT_EQ(10, monitor_->maxResourceUsage());
  EXPECT_TRUE(monitor_->tryAllocateResource(3));
  EXPECT_EQ(3, monitor_->currentResourceUsage());
  EXPECT_TRUE(monitor_->tryDeallocateResource(2));
  EXPECT_EQ(1, monitor_->currentResourceUsage());
}

TEST_F(ActiveDownstreamConnectionsMonitorTest, FailsToAllocateDeallocateWhenMinMaxHit) {
  envoy::extensions::resource_monitors::downstream_connections::v3::DownstreamConnectionsConfig
      config;
  config.set_max_active_downstream_connections(1);
  initialize(config);
  EXPECT_EQ(0, monitor_->currentResourceUsage());
  EXPECT_EQ(1, monitor_->maxResourceUsage());
  EXPECT_TRUE(monitor_->tryAllocateResource(1));
  EXPECT_FALSE(monitor_->tryAllocateResource(1));
  EXPECT_TRUE(monitor_->tryDeallocateResource(1));
  EXPECT_EQ(0, monitor_->currentResourceUsage());
  EXPECT_FALSE(monitor_->tryDeallocateResource(1));
}

TEST_F(ActiveDownstreamConnectionsMonitorTest, AllocateCasMultithreaded) {
  envoy::extensions::resource_monitors::downstream_connections::v3::DownstreamConnectionsConfig
      config;
  config.set_max_active_downstream_connections(1);
  initialize(config);
  synchronizer().enable();
  // Start a thread and wait pre-CAS.
  synchronizer().waitOn("try_allocate_pre_cas");
  std::thread t1([&] { EXPECT_FALSE(monitor_->tryAllocateResource(1)); });
  // Wait until the thread is actually waiting.
  synchronizer().barrierOn("try_allocate_pre_cas");
  // Increase connection counter to 1, which should cause the CAS to fail on the other thread.
  EXPECT_TRUE(monitor_->tryAllocateResource(1));
  synchronizer().signal("try_allocate_pre_cas");
  t1.join();
}

TEST_F(ActiveDownstreamConnectionsMonitorTest, DeallocateCasMultithreaded) {
  envoy::extensions::resource_monitors::downstream_connections::v3::DownstreamConnectionsConfig
      config;
  config.set_max_active_downstream_connections(3);
  initialize(config);
  synchronizer().enable();
  EXPECT_TRUE(monitor_->tryAllocateResource(3));
  // Start a thread and wait pre-CAS.
  synchronizer().waitOn("try_deallocate_pre_cas");
  std::thread t1([&] { EXPECT_FALSE(monitor_->tryDeallocateResource(1)); });
  // Wait until the thread is actually waiting.
  synchronizer().barrierOn("try_deallocate_pre_cas");
  EXPECT_TRUE(monitor_->tryDeallocateResource(3));
  synchronizer().signal("try_deallocate_pre_cas");
  t1.join();
}

} // namespace DownstreamConnections
} // namespace ResourceMonitors
} // namespace Extensions
} // namespace Envoy
