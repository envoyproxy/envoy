#include "envoy/config/resource_monitor/unused_heap/v2alpha/unused_heap.pb.h"

#include "extensions/resource_monitors/common/memory_stats_reader.h"
#include "extensions/resource_monitors/unused_heap/unused_heap_monitor.h"

#include "test/extensions/resource_monitors/mocks.h"

#include "absl/types/optional.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace ResourceMonitors {
namespace UnusedHeapMonitor {
namespace {

class ResourcePressure : public Server::ResourceMonitor::Callbacks {
public:
  void onSuccess(const Server::ResourceUsage& usage) override {
    pressure_ = usage.resource_pressure_;
  }

  void onFailure(const EnvoyException& error) override { error_ = error; }

  bool hasPressure() const { return pressure_.has_value(); }
  bool hasError() const { return error_.has_value(); }

  double pressure() const { return *pressure_; }

private:
  absl::optional<double> pressure_;
  absl::optional<EnvoyException> error_;
};

TEST(UnusedHeapMonitorTest, ComputesCorrectUsage) {
  {
    envoy::config::resource_monitor::unused_heap::v2alpha::UnusedHeapConfig config;
    config.set_max_unused_heap_size_bytes(1000);
    auto stats_reader = std::make_unique<MockMemoryStatsReader>();
    EXPECT_CALL(*stats_reader, reservedHeapBytes()).WillOnce(testing::Return(800));
    EXPECT_CALL(*stats_reader, unmappedHeapBytes()).WillOnce(testing::Return(100));
    EXPECT_CALL(*stats_reader, allocatedHeapBytes()).WillOnce(testing::Return(600));
    std::unique_ptr<UnusedHeapMonitor> monitor(
        new UnusedHeapMonitor(config, std::move(stats_reader)));
    ResourcePressure resource;
    monitor->updateResourceUsage(resource);
    EXPECT_TRUE(resource.hasPressure());
    EXPECT_FALSE(resource.hasError());
    EXPECT_EQ(resource.pressure(), 0.1);
  }
  {
    envoy::config::resource_monitor::unused_heap::v2alpha::UnusedHeapConfig config;
    config.mutable_max_unused_heap_percent()->set_value(50.0);
    auto stats_reader = std::make_unique<MockMemoryStatsReader>();
    EXPECT_CALL(*stats_reader, reservedHeapBytes()).WillOnce(testing::Return(800));
    EXPECT_CALL(*stats_reader, unmappedHeapBytes()).WillOnce(testing::Return(100));
    EXPECT_CALL(*stats_reader, allocatedHeapBytes()).WillOnce(testing::Return(350));
    std::unique_ptr<UnusedHeapMonitor> monitor(
        new UnusedHeapMonitor(config, std::move(stats_reader)));
    ResourcePressure resource;
    monitor->updateResourceUsage(resource);
    EXPECT_TRUE(resource.hasPressure());
    EXPECT_FALSE(resource.hasError());
    EXPECT_EQ(resource.pressure(), 1.0);
  }
}

} // namespace
} // namespace UnusedHeapMonitor
} // namespace ResourceMonitors
} // namespace Extensions
} // namespace Envoy
