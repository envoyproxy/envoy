#include "source/extensions/resource_monitors/fixed_heap/fixed_heap_monitor.h"

#include "test/mocks/runtime/mocks.h"
#include "test/test_common/test_runtime.h"

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
  MOCK_METHOD(uint64_t, allocatedHeapBytes, ());
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
  auto stats_reader = std::make_unique<MockMemoryStatsReader>();
  EXPECT_CALL(*stats_reader, reservedHeapBytes()).WillOnce(Return(800));
  EXPECT_CALL(*stats_reader, unmappedHeapBytes()).WillOnce(Return(100));
  EXPECT_CALL(*stats_reader, freeMappedHeapBytes()).WillOnce(Return(200));
  auto monitor = std::make_unique<FixedHeapMonitor>(
      absl::variant<uint64_t, Runtime::UInt64>(uint64_t(1000)), std::move(stats_reader));

  ResourcePressure resource;
  monitor->updateResourceUsage(resource);
  ASSERT_TRUE(resource.hasPressure());
  ASSERT_FALSE(resource.hasError());
  EXPECT_EQ(resource.pressure(), 0.5);
}

TEST(FixedHeapMonitorTest, ComputesCorrectUsageRuntimeUseAllocated) {
  TestScopedRuntime scoped_runtime;
  scoped_runtime.mergeValues({{"envoy.reloadable_features.fixed_heap_use_allocated", "true"}});

  auto stats_reader = std::make_unique<MockMemoryStatsReader>();
  EXPECT_CALL(*stats_reader, allocatedHeapBytes()).WillOnce(Return(600));
  auto monitor = std::make_unique<FixedHeapMonitor>(
      absl::variant<uint64_t, Runtime::UInt64>(uint64_t(1000)), std::move(stats_reader));

  ResourcePressure resource;
  monitor->updateResourceUsage(resource);
  ASSERT_TRUE(resource.hasPressure());
  ASSERT_FALSE(resource.hasError());
  EXPECT_EQ(resource.pressure(), 0.6);
}

TEST(FixedHeapMonitorTest, ComputeUsageWithRealMemoryStats) {
  const uint64_t max_heap = 1024 * 1024 * 1024;
  auto stats_reader = std::make_unique<MemoryStatsReader>();
  const double expected_usage =
      (stats_reader->reservedHeapBytes() - stats_reader->unmappedHeapBytes() -
       stats_reader->freeMappedHeapBytes()) /
      static_cast<double>(max_heap);
  auto monitor = std::make_unique<FixedHeapMonitor>(
      absl::variant<uint64_t, Runtime::UInt64>(max_heap), std::move(stats_reader));

  ResourcePressure resource;
  monitor->updateResourceUsage(resource);
  EXPECT_NEAR(resource.pressure(), expected_usage, 0.0005);
}

TEST(FixedHeapMonitorTest, ComputesUsageRuntimeUseAllocatedWithRealMemoryStats) {
  TestScopedRuntime scoped_runtime;
  scoped_runtime.mergeValues({{"envoy.reloadable_features.fixed_heap_use_allocated", "true"}});

  const uint64_t max_heap = 1024 * 1024 * 1024;
  auto stats_reader = std::make_unique<MemoryStatsReader>();
  const double expected_pressure =
      stats_reader->allocatedHeapBytes() / static_cast<double>(max_heap);
  auto monitor = std::make_unique<FixedHeapMonitor>(
      absl::variant<uint64_t, Runtime::UInt64>(max_heap), std::move(stats_reader));

  ResourcePressure resource;
  monitor->updateResourceUsage(resource);
  EXPECT_NEAR(resource.pressure(), expected_pressure, 0.0005);
}

TEST(FixedHeapMonitorTest, RuntimeMaxHeapReReadOnEachUpdate) {
  testing::NiceMock<Runtime::MockLoader> runtime;
  EXPECT_CALL(runtime, snapshot()).WillRepeatedly(testing::ReturnRef(runtime.snapshot_));

  envoy::config::core::v3::RuntimeUInt64 runtime_proto;
  runtime_proto.set_runtime_key("fixed_heap.max_bytes");
  runtime_proto.set_default_value(1000);

  EXPECT_CALL(runtime.snapshot_, getInteger("fixed_heap.max_bytes", 1000))
      .WillOnce(Return(1000)) // first updateResourceUsage
      .WillOnce(Return(500)); // second updateResourceUsage (changed at runtime)

  auto stats_reader = std::make_unique<MockMemoryStatsReader>();
  EXPECT_CALL(*stats_reader, reservedHeapBytes()).WillRepeatedly(Return(800));
  EXPECT_CALL(*stats_reader, unmappedHeapBytes()).WillRepeatedly(Return(100));
  EXPECT_CALL(*stats_reader, freeMappedHeapBytes()).WillRepeatedly(Return(200));

  Runtime::UInt64 runtime_uint64(runtime_proto, runtime);
  auto monitor = std::make_unique<FixedHeapMonitor>(
      absl::variant<uint64_t, Runtime::UInt64>(std::move(runtime_uint64)), std::move(stats_reader));

  ResourcePressure resource1;
  monitor->updateResourceUsage(resource1);
  ASSERT_TRUE(resource1.hasPressure());
  EXPECT_EQ(resource1.pressure(), 0.5); // 500 / 1000

  ResourcePressure resource2;
  monitor->updateResourceUsage(resource2);
  ASSERT_TRUE(resource2.hasPressure());
  EXPECT_EQ(resource2.pressure(), 1.0); // 500 / 500
}

TEST(FixedHeapMonitorTest, RuntimeMaxHeapZeroAtRuntime) {
  testing::NiceMock<Runtime::MockLoader> runtime;
  EXPECT_CALL(runtime, snapshot()).WillRepeatedly(testing::ReturnRef(runtime.snapshot_));

  envoy::config::core::v3::RuntimeUInt64 runtime_proto;
  runtime_proto.set_runtime_key("fixed_heap.max_bytes");
  runtime_proto.set_default_value(1000);

  EXPECT_CALL(runtime.snapshot_, getInteger("fixed_heap.max_bytes", 1000))
      .WillOnce(Return(0)); // updateResourceUsage returns 0 at runtime

  auto stats_reader = std::make_unique<MockMemoryStatsReader>();
  Runtime::UInt64 runtime_uint64(runtime_proto, runtime);
  auto monitor = std::make_unique<FixedHeapMonitor>(
      absl::variant<uint64_t, Runtime::UInt64>(std::move(runtime_uint64)), std::move(stats_reader));

  ResourcePressure resource;
  monitor->updateResourceUsage(resource);
  ASSERT_TRUE(resource.hasError());
  ASSERT_FALSE(resource.hasPressure());
}

} // namespace
} // namespace FixedHeapMonitor
} // namespace ResourceMonitors
} // namespace Extensions
} // namespace Envoy
