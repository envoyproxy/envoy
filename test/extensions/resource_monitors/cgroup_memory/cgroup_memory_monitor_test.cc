#include "source/extensions/resource_monitors/cgroup_memory/cgroup_memory_monitor.h"
#include "source/extensions/resource_monitors/cgroup_memory/cgroup_memory_stats_reader.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::Return;

namespace Envoy {
namespace Extensions {
namespace ResourceMonitors {
namespace CgroupMemory {
namespace {

class MockCgroupMemoryStatsReader : public CgroupMemoryStatsReader {
public:
  MOCK_METHOD(uint64_t, getMemoryUsage, (), (override));
  MOCK_METHOD(uint64_t, getMemoryLimit, (), (override));
  MOCK_METHOD(std::string, getMemoryUsagePath, (), (const, override));
  MOCK_METHOD(std::string, getMemoryLimitPath, (), (const, override));
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
  const EnvoyException& error() const { return *error_; }

private:
  absl::optional<double> pressure_;
  absl::optional<EnvoyException> error_;
};

// Test that the monitor computes correct usage using the configured limit
TEST(CgroupMemoryMonitorTest, ComputesCorrectUsageUsingConfigLimit) {
  envoy::extensions::resource_monitors::cgroup_memory::v3::CgroupMemoryConfig config;
  config.mutable_max_memory_bytes()->set_value(1000);

  auto stats_reader = std::make_unique<MockCgroupMemoryStatsReader>();
  EXPECT_CALL(*stats_reader, getMemoryUsage()).WillOnce(Return(500));
  EXPECT_CALL(*stats_reader, getMemoryLimit()).WillOnce(Return(500));

  auto monitor = std::make_unique<CgroupMemoryMonitor>(config, std::move(stats_reader));

  ResourcePressure resource;
  monitor->updateResourceUsage(resource);
  ASSERT_TRUE(resource.hasPressure());
  ASSERT_FALSE(resource.hasError());
  EXPECT_DOUBLE_EQ(resource.pressure(), 0.5);
}

// Test that the monitor computes correct usage using the cgroup limit
TEST(CgroupMemoryMonitorTest, ComputesCorrectUsageUsingCgroupLimit) {
  envoy::extensions::resource_monitors::cgroup_memory::v3::CgroupMemoryConfig config;

  auto stats_reader = std::make_unique<MockCgroupMemoryStatsReader>();
  EXPECT_CALL(*stats_reader, getMemoryUsage()).WillOnce(Return(500));
  EXPECT_CALL(*stats_reader, getMemoryLimit()).WillOnce(Return(2000));

  auto monitor = std::make_unique<CgroupMemoryMonitor>(config, std::move(stats_reader));

  ResourcePressure resource;
  monitor->updateResourceUsage(resource);
  ASSERT_TRUE(resource.hasPressure());
  ASSERT_FALSE(resource.hasError());
  EXPECT_DOUBLE_EQ(resource.pressure(), 0.25);
}

// Test that the monitor reports correct pressure when usage exceeds the limit
TEST(CgroupMemoryMonitorTest, UsageExceedsLimit) {
  envoy::extensions::resource_monitors::cgroup_memory::v3::CgroupMemoryConfig config;

  auto stats_reader = std::make_unique<MockCgroupMemoryStatsReader>();
  EXPECT_CALL(*stats_reader, getMemoryUsage()).WillOnce(Return(2000)); // Usage higher than limit
  EXPECT_CALL(*stats_reader, getMemoryLimit()).WillOnce(Return(1000));

  auto monitor = std::make_unique<CgroupMemoryMonitor>(config, std::move(stats_reader));

  ResourcePressure resource;
  monitor->updateResourceUsage(resource);
  ASSERT_TRUE(resource.hasPressure());
  ASSERT_FALSE(resource.hasError());
  // Should report pressure > 1.0 indicating over-utilization
  EXPECT_DOUBLE_EQ(resource.pressure(), 2.0);
}

// Test that the monitor handles unlimited cgroup memory
TEST(CgroupMemoryMonitorTest, HandlesUnlimitedCgroupMemory) {
  envoy::extensions::resource_monitors::cgroup_memory::v3::CgroupMemoryConfig config;

  auto stats_reader = std::make_unique<MockCgroupMemoryStatsReader>();
  EXPECT_CALL(*stats_reader, getMemoryUsage()).WillOnce(Return(500));
  EXPECT_CALL(*stats_reader, getMemoryLimit())
      .WillOnce(Return(CgroupMemoryStatsReader::UNLIMITED_MEMORY));

  auto monitor = std::make_unique<CgroupMemoryMonitor>(config, std::move(stats_reader));

  ResourcePressure resource;
  monitor->updateResourceUsage(resource);
  ASSERT_TRUE(resource.hasPressure());
  ASSERT_FALSE(resource.hasError());
  // Unlimited cgroup memory means no pressure
  EXPECT_DOUBLE_EQ(resource.pressure(), 0.0);
}

// Test that the monitor handles unlimited configured memory
TEST(CgroupMemoryMonitorTest, HandlesUnlimitedConfigMemory) {
  envoy::extensions::resource_monitors::cgroup_memory::v3::CgroupMemoryConfig config;
  config.mutable_max_memory_bytes()->set_value(CgroupMemoryStatsReader::UNLIMITED_MEMORY);

  auto stats_reader = std::make_unique<MockCgroupMemoryStatsReader>();
  EXPECT_CALL(*stats_reader, getMemoryUsage()).WillOnce(Return(500));
  EXPECT_CALL(*stats_reader, getMemoryLimit()).WillOnce(Return(500));

  auto monitor = std::make_unique<CgroupMemoryMonitor>(config, std::move(stats_reader));

  ResourcePressure resource;
  monitor->updateResourceUsage(resource);
  EXPECT_TRUE(resource.hasPressure());
  EXPECT_FALSE(resource.hasError());
  EXPECT_DOUBLE_EQ(resource.pressure(),
                   0.0); // Should report no pressure when cgroup limit is unlimited
}

// Test that the monitor handles errors from the stats reader
TEST(CgroupMemoryMonitorTest, HandlesErrorFromStatsReader) {
  envoy::extensions::resource_monitors::cgroup_memory::v3::CgroupMemoryConfig config;

  auto stats_reader = std::make_unique<MockCgroupMemoryStatsReader>();
  EXPECT_CALL(*stats_reader, getMemoryUsage())
      .WillOnce(testing::Throw(EnvoyException("Failed to read memory usage")));

  auto monitor = std::make_unique<CgroupMemoryMonitor>(config, std::move(stats_reader));

  ResourcePressure resource;
  monitor->updateResourceUsage(resource);
  EXPECT_FALSE(resource.hasPressure());
  EXPECT_TRUE(resource.hasError());
  EXPECT_THAT(resource.error().what(), testing::HasSubstr("Failed to read memory usage"));
}

// Test that the monitor handles both limits being unlimited
TEST(CgroupMemoryMonitorTest, HandlesBothLimitsUnlimited) {
  envoy::extensions::resource_monitors::cgroup_memory::v3::CgroupMemoryConfig config;
  config.mutable_max_memory_bytes()->set_value(CgroupMemoryStatsReader::UNLIMITED_MEMORY);

  auto stats_reader = std::make_unique<MockCgroupMemoryStatsReader>();
  EXPECT_CALL(*stats_reader, getMemoryUsage()).WillOnce(Return(500));
  EXPECT_CALL(*stats_reader, getMemoryLimit())
      .WillOnce(Return(CgroupMemoryStatsReader::UNLIMITED_MEMORY));

  auto monitor = std::make_unique<CgroupMemoryMonitor>(config, std::move(stats_reader));

  ResourcePressure resource;
  monitor->updateResourceUsage(resource);
  EXPECT_TRUE(resource.hasPressure());
  EXPECT_FALSE(resource.hasError());
  EXPECT_DOUBLE_EQ(resource.pressure(), 0.0);
}

} // namespace
} // namespace CgroupMemory
} // namespace ResourceMonitors
} // namespace Extensions
} // namespace Envoy
