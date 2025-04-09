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

private:
  absl::optional<double> pressure_;
  absl::optional<EnvoyException> error_;
};

TEST(CgroupMemoryMonitorTest, ComputesCorrectUsage) {
  envoy::extensions::resource_monitors::cgroup_memory::v3::CgroupMemoryConfig config;
  config.set_max_memory_bytes(1000);

  auto stats_reader = std::make_unique<MockCgroupMemoryStatsReader>();
  EXPECT_CALL(*stats_reader, getMemoryUsage()).WillOnce(Return(500));
  EXPECT_CALL(*stats_reader, getMemoryLimit()).WillOnce(Return(1000));

  auto monitor = std::make_unique<CgroupMemoryMonitor>(config, std::move(stats_reader));

  ResourcePressure resource;
  monitor->updateResourceUsage(resource);
  ASSERT_TRUE(resource.hasPressure());
  ASSERT_FALSE(resource.hasError());
  EXPECT_DOUBLE_EQ(resource.pressure(), 0.5);
}

TEST(CgroupMemoryMonitorTest, ReportsErrorOnZeroLimit) {
  envoy::extensions::resource_monitors::cgroup_memory::v3::CgroupMemoryConfig config;
  config.set_max_memory_bytes(0);

  auto stats_reader = std::make_unique<MockCgroupMemoryStatsReader>();
  EXPECT_CALL(*stats_reader, getMemoryUsage()).WillOnce(Return(500));
  EXPECT_CALL(*stats_reader, getMemoryLimit()).WillOnce(Return(0));

  auto monitor = std::make_unique<CgroupMemoryMonitor>(config, std::move(stats_reader));

  ResourcePressure resource;
  monitor->updateResourceUsage(resource);
  ASSERT_TRUE(resource.hasError());
  ASSERT_FALSE(resource.hasPressure());
}

TEST(CgroupMemoryMonitorTest, UsesConfiguredLimitWhenLowerThanCgroup) {
  envoy::extensions::resource_monitors::cgroup_memory::v3::CgroupMemoryConfig config;
  config.set_max_memory_bytes(1000); // Lower than cgroup limit

  auto stats_reader = std::make_unique<MockCgroupMemoryStatsReader>();
  EXPECT_CALL(*stats_reader, getMemoryUsage()).WillOnce(Return(500));
  EXPECT_CALL(*stats_reader, getMemoryLimit()).WillOnce(Return(2000)); // Higher cgroup limit

  auto monitor = std::make_unique<CgroupMemoryMonitor>(config, std::move(stats_reader));

  ResourcePressure resource;
  monitor->updateResourceUsage(resource);
  ASSERT_TRUE(resource.hasPressure());
  ASSERT_FALSE(resource.hasError());
  // Should use configured limit (1000) for calculation: 500/1000 = 0.5
  EXPECT_DOUBLE_EQ(resource.pressure(), 0.5);
}

TEST(CgroupMemoryMonitorTest, UsesCgroupLimitWhenLowerThanConfigured) {
  envoy::extensions::resource_monitors::cgroup_memory::v3::CgroupMemoryConfig config;
  config.set_max_memory_bytes(2000); // Higher than cgroup limit

  auto stats_reader = std::make_unique<MockCgroupMemoryStatsReader>();
  EXPECT_CALL(*stats_reader, getMemoryUsage()).WillOnce(Return(500));
  EXPECT_CALL(*stats_reader, getMemoryLimit()).WillOnce(Return(1000)); // Lower cgroup limit

  auto monitor = std::make_unique<CgroupMemoryMonitor>(config, std::move(stats_reader));

  ResourcePressure resource;
  monitor->updateResourceUsage(resource);
  ASSERT_TRUE(resource.hasPressure());
  ASSERT_FALSE(resource.hasError());
  // Should use cgroup limit (1000) for calculation: 500/1000 = 0.5
  EXPECT_DOUBLE_EQ(resource.pressure(), 0.5);
}

TEST(CgroupMemoryMonitorTest, UsageExceedsLimit) {
  envoy::extensions::resource_monitors::cgroup_memory::v3::CgroupMemoryConfig config;
  config.set_max_memory_bytes(1000);

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

TEST(CgroupMemoryMonitorTest, HandlesVeryLargeValues) {
  envoy::extensions::resource_monitors::cgroup_memory::v3::CgroupMemoryConfig config;
  config.set_max_memory_bytes(std::numeric_limits<uint64_t>::max());

  auto stats_reader = std::make_unique<MockCgroupMemoryStatsReader>();
  EXPECT_CALL(*stats_reader, getMemoryUsage()).WillOnce(Return(std::numeric_limits<uint64_t>::max() / 2));
  EXPECT_CALL(*stats_reader, getMemoryLimit()).WillOnce(Return(std::numeric_limits<uint64_t>::max()));

  auto monitor = std::make_unique<CgroupMemoryMonitor>(config, std::move(stats_reader));

  ResourcePressure resource;
  monitor->updateResourceUsage(resource);
  ASSERT_TRUE(resource.hasPressure());
  ASSERT_FALSE(resource.hasError());
  // Should handle large values correctly: max/2 / max = 0.5
  EXPECT_DOUBLE_EQ(resource.pressure(), 0.5);
}

} // namespace
} // namespace CgroupMemory
} // namespace ResourceMonitors
} // namespace Extensions
} // namespace Envoy