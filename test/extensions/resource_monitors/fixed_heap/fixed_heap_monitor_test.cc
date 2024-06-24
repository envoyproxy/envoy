#include "envoy/extensions/resource_monitors/fixed_heap/v3/fixed_heap.pb.h"

#include "source/extensions/resource_monitors/fixed_heap/fixed_heap_monitor.h"

#include "absl/types/optional.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace ResourceMonitors {
namespace FixedHeapMonitor {
namespace {

using testing::Return;

class MockMemoryStatsReader : public MemoryStatsReader {
public:
  MockMemoryStatsReader() = default;

  MOCK_METHOD(uint64_t, reservedHeapBytes, ());
  MOCK_METHOD(uint64_t, unmappedHeapBytes, ());
  MOCK_METHOD(uint64_t, freeMappedHeapBytes, ());
};

class ResourcePressure : public Server::ResourceUpdateCallbacks {
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

  envoy::extensions::resource_monitors::fixed_heap::v3::FixedHeapConfig config;
  config.set_max_heap_size_bytes(1000);
  auto stats_reader = std::make_unique<MockMemoryStatsReader>();
  EXPECT_CALL(*stats_reader, reservedHeapBytes()).WillOnce(Return(800));
  EXPECT_CALL(*stats_reader, unmappedHeapBytes()).WillOnce(Return(100));
  EXPECT_CALL(*stats_reader, freeMappedHeapBytes()).WillOnce(Return(200));
  auto monitor = std::make_unique<FixedHeapMonitor>(config, std::move(stats_reader));

  ResourcePressure resource;
  monitor->updateResourceUsage(resource);
  ASSERT_TRUE(resource.hasPressure());
  ASSERT_FALSE(resource.hasError());
  EXPECT_EQ(resource.pressure(), 0.5);
}

TEST(FixedHeapMonitorTest, ComputeUsageWithRealMemoryStats) {

  envoy::extensions::resource_monitors::fixed_heap::v3::FixedHeapConfig config;
  const uint64_t max_heap = 1024 * 1024 * 1024;
  config.set_max_heap_size_bytes(max_heap);
  auto stats_reader = std::make_unique<MemoryStatsReader>();
  const double expected_usage =
      (stats_reader->reservedHeapBytes() - stats_reader->unmappedHeapBytes() -
       stats_reader->freeMappedHeapBytes()) /
      static_cast<double>(max_heap);
  auto monitor = std::make_unique<FixedHeapMonitor>(config, std::move(stats_reader));

  ResourcePressure resource;
  monitor->updateResourceUsage(resource);
  EXPECT_NEAR(resource.pressure(), expected_usage, 0.0005);
}
} // namespace
} // namespace FixedHeapMonitor
} // namespace ResourceMonitors
} // namespace Extensions
} // namespace Envoy
