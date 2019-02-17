#include "extensions/resource_monitors/fixed_heap/fixed_heap_monitor.h"

#include "absl/types/optional.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace ResourceMonitors {
namespace FixedHeapMonitor {

class MockMemoryStatsReader : public MemoryStatsReader {
public:
  MockMemoryStatsReader() {}

  MOCK_METHOD0(reservedHeapBytes, uint64_t());
  MOCK_METHOD0(unmappedHeapBytes, uint64_t());
};

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

TEST(FixedHeapMonitorTest, ComputesCorrectUsage) {
  envoy::config::resource_monitor::fixed_heap::v2alpha::FixedHeapConfig config;
  config.set_max_heap_size_bytes(1000);
  auto stats_reader = std::make_unique<MockMemoryStatsReader>();
  EXPECT_CALL(*stats_reader, reservedHeapBytes()).WillOnce(testing::Return(800));
  EXPECT_CALL(*stats_reader, unmappedHeapBytes()).WillOnce(testing::Return(100));
  std::unique_ptr<FixedHeapMonitor> monitor(new FixedHeapMonitor(config, std::move(stats_reader)));

  ResourcePressure resource;
  monitor->updateResourceUsage(resource);
  EXPECT_TRUE(resource.hasPressure());
  EXPECT_FALSE(resource.hasError());
  EXPECT_EQ(resource.pressure(), 0.7);
}

} // namespace FixedHeapMonitor
} // namespace ResourceMonitors
} // namespace Extensions
} // namespace Envoy
