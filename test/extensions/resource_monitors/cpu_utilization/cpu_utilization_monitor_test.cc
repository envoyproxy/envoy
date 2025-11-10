#include <cstdlib>

#include "envoy/extensions/resource_monitors/cpu_utilization/v3/cpu_utilization.pb.h"

#include "source/extensions/resource_monitors/cpu_utilization/cpu_stats_reader.h"
#include "source/extensions/resource_monitors/cpu_utilization/cpu_utilization_monitor.h"

#include "absl/types/optional.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace ResourceMonitors {
namespace CpuUtilizationMonitor {
namespace {

using testing::Return;

class MockCpuStatsReader : public CpuStatsReader {
public:
  MockCpuStatsReader() = default;

  MOCK_METHOD(CpuTimes, getCpuTimes, ());
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

TEST(HostCpuUtilizationMonitorTest, ComputesCorrectUsageCgroupV1) {
  envoy::extensions::resource_monitors::cpu_utilization::v3::CpuUtilizationConfig config;
  auto stats_reader = std::make_unique<MockCpuStatsReader>();
  EXPECT_CALL(*stats_reader, getCpuTimes())
      .WillOnce(Return(CpuTimes{true, false, 50, 100, 0}))   // cgroup v1
      .WillOnce(Return(CpuTimes{true, false, 100, 200, 0}))  // cgroup v1
      .WillOnce(Return(CpuTimes{true, false, 200, 300, 0})); // cgroup v1
  auto monitor = std::make_unique<CpuUtilizationMonitor>(config, std::move(stats_reader));

  ResourcePressure resource;
  monitor->updateResourceUsage(resource);
  ASSERT_TRUE(resource.hasPressure());
  ASSERT_FALSE(resource.hasError());
  EXPECT_DOUBLE_EQ(resource.pressure(), 0.025); // dampening

  monitor->updateResourceUsage(resource);
  ASSERT_TRUE(resource.hasPressure());
  ASSERT_FALSE(resource.hasError());
  EXPECT_DOUBLE_EQ(resource.pressure(), 0.07375); // dampening
}

TEST(HostCpuUtilizationMonitorTest, ComputesCorrectUsageCgroupV2) {
  envoy::extensions::resource_monitors::cpu_utilization::v3::CpuUtilizationConfig config;
  auto stats_reader = std::make_unique<MockCpuStatsReader>();
  // For cgroup v2: work_time is in microseconds, total_time is in nanoseconds, effective_cores must
  // be > 0 Formula: current_utilization = ((work_over_period / 1000000.0) /
  // (total_over_period_seconds * effective_cores)) where total_over_period_seconds =
  // total_over_period / 1000000000.0
  EXPECT_CALL(*stats_reader, getCpuTimes())
      .WillOnce(Return(CpuTimes{true, true, 50000.0, 100000000,
                                2.0})) // cgroup v2: 50ms work, 100ms total, 2 cores
      .WillOnce(Return(CpuTimes{true, true, 100000.0, 200000000,
                                2.0})) // cgroup v2: 100ms work, 200ms total, 2 cores
      .WillOnce(Return(CpuTimes{true, true, 200000.0, 400000000,
                                2.0})); // cgroup v2: 200ms work, 400ms total, 2 cores
  auto monitor = std::make_unique<CpuUtilizationMonitor>(config, std::move(stats_reader));

  ResourcePressure resource;
  // First update: work_over_period=50000, total_over_period=100000000
  // total_over_period_seconds = 0.1, current_utilization = (0.05 / (0.1 * 2)) = 0.25
  // utilization = 0.25 * 0.05 = 0.0125
  monitor->updateResourceUsage(resource);
  ASSERT_TRUE(resource.hasPressure());
  ASSERT_FALSE(resource.hasError());
  EXPECT_DOUBLE_EQ(resource.pressure(), 0.0125);

  // Second update: work_over_period=50000, total_over_period=100000000
  // current_utilization = 0.25, utilization = 0.25 * 0.05 + 0.0125 * 0.95 = 0.024375
  monitor->updateResourceUsage(resource);
  ASSERT_TRUE(resource.hasPressure());
  ASSERT_FALSE(resource.hasError());
  EXPECT_DOUBLE_EQ(resource.pressure(), 0.024375);
}

TEST(HostCpuUtilizationMonitorTest, GetsErroneousStatsDenominator) {
  envoy::extensions::resource_monitors::cpu_utilization::v3::CpuUtilizationConfig config;
  auto stats_reader = std::make_unique<MockCpuStatsReader>();
  EXPECT_CALL(*stats_reader, getCpuTimes())
      .WillOnce(Return(CpuTimes{true, false, 100, 100, 0})) // cgroup v1
      .WillOnce(Return(CpuTimes{true, false, 100, 99, 0})); // cgroup v1
  auto monitor = std::make_unique<CpuUtilizationMonitor>(config, std::move(stats_reader));
  ResourcePressure resource;
  monitor->updateResourceUsage(resource);
  ASSERT_TRUE(resource.hasError());
}

TEST(HostCpuUtilizationMonitorTest, GetsErroneousStatsNumerator) {
  envoy::extensions::resource_monitors::cpu_utilization::v3::CpuUtilizationConfig config;
  auto stats_reader = std::make_unique<MockCpuStatsReader>();
  EXPECT_CALL(*stats_reader, getCpuTimes())
      .WillOnce(Return(CpuTimes{true, false, 100, 100, 0})) // cgroup v1
      .WillOnce(Return(CpuTimes{true, false, 99, 150, 0})); // cgroup v1
  auto monitor = std::make_unique<CpuUtilizationMonitor>(config, std::move(stats_reader));

  ResourcePressure resource;
  monitor->updateResourceUsage(resource);
  ASSERT_TRUE(resource.hasError());
}

TEST(HostCpuUtilizationMonitorTest, ReportsError) {
  envoy::extensions::resource_monitors::cpu_utilization::v3::CpuUtilizationConfig config;
  auto stats_reader = std::make_unique<MockCpuStatsReader>();
  EXPECT_CALL(*stats_reader, getCpuTimes())
      .WillOnce(Return(CpuTimes{false, false, 0, 0, 0}))    // cgroup v1
      .WillOnce(Return(CpuTimes{false, false, 0, 200, 0}))  // cgroup v1
      .WillOnce(Return(CpuTimes{false, false, 0, 300, 0})); // cgroup v1
  auto monitor = std::make_unique<CpuUtilizationMonitor>(config, std::move(stats_reader));

  ResourcePressure resource;
  monitor->updateResourceUsage(resource);
  ASSERT_TRUE(resource.hasError());

  monitor->updateResourceUsage(resource);
  ASSERT_TRUE(resource.hasError());
}

TEST(ContainerCpuUsageMonitorTest, ComputesCorrectUsage) {
  envoy::extensions::resource_monitors::cpu_utilization::v3::CpuUtilizationConfig config;
  config.set_mode(
      envoy::extensions::resource_monitors::cpu_utilization::v3::CpuUtilizationConfig::CONTAINER);
  auto stats_reader = std::make_unique<MockCpuStatsReader>();
  EXPECT_CALL(*stats_reader, getCpuTimes())
      .WillOnce(Return(CpuTimes{true, false, 1101, 1001, 0}))  // cgroup v1
      .WillOnce(Return(CpuTimes{true, false, 1102, 1002, 0}))  // cgroup v1
      .WillOnce(Return(CpuTimes{true, false, 1103, 1003, 0})); // cgroup v1
  auto monitor = std::make_unique<CpuUtilizationMonitor>(config, std::move(stats_reader));

  ResourcePressure resource;
  monitor->updateResourceUsage(resource);
  ASSERT_TRUE(resource.hasPressure());
  ASSERT_FALSE(resource.hasError());
  EXPECT_DOUBLE_EQ(resource.pressure(), 0.05);

  monitor->updateResourceUsage(resource);
  ASSERT_TRUE(resource.hasPressure());
  ASSERT_FALSE(resource.hasError());
  EXPECT_DOUBLE_EQ(resource.pressure(), 0.0975);
}

TEST(ContainerCpuUsageMonitorTest, GetsErroneousStatsDenominator) {
  envoy::extensions::resource_monitors::cpu_utilization::v3::CpuUtilizationConfig config;
  config.set_mode(
      envoy::extensions::resource_monitors::cpu_utilization::v3::CpuUtilizationConfig::CONTAINER);
  auto stats_reader = std::make_unique<MockCpuStatsReader>();
  EXPECT_CALL(*stats_reader, getCpuTimes())
      .WillOnce(Return(CpuTimes{true, false, 1000, 100, 0})) // cgroup v1
      .WillOnce(Return(CpuTimes{true, false, 1001, 99, 0})); // cgroup v1
  auto monitor = std::make_unique<CpuUtilizationMonitor>(config, std::move(stats_reader));
  ResourcePressure resource;
  monitor->updateResourceUsage(resource);
  ASSERT_TRUE(resource.hasError());
}

TEST(ContainerCpuUsageMonitorTest, GetsErroneousStatsNumerator) {
  envoy::extensions::resource_monitors::cpu_utilization::v3::CpuUtilizationConfig config;
  config.set_mode(
      envoy::extensions::resource_monitors::cpu_utilization::v3::CpuUtilizationConfig::CONTAINER);
  auto stats_reader = std::make_unique<MockCpuStatsReader>();
  EXPECT_CALL(*stats_reader, getCpuTimes())
      .WillOnce(Return(CpuTimes{true, false, 1000, 101, 0})) // cgroup v1
      .WillOnce(Return(CpuTimes{true, false, 999, 102, 0})); // cgroup v1
  auto monitor = std::make_unique<CpuUtilizationMonitor>(config, std::move(stats_reader));
  ResourcePressure resource;
  monitor->updateResourceUsage(resource);
  ASSERT_TRUE(resource.hasError());
}

TEST(ContainerCpuUtilizationMonitorTest, ReportsError) {
  envoy::extensions::resource_monitors::cpu_utilization::v3::CpuUtilizationConfig config;
  config.set_mode(
      envoy::extensions::resource_monitors::cpu_utilization::v3::CpuUtilizationConfig::CONTAINER);
  auto stats_reader = std::make_unique<MockCpuStatsReader>();
  EXPECT_CALL(*stats_reader, getCpuTimes())
      .WillOnce(Return(CpuTimes{false, false, 0, 0, 0}))    // cgroup v1
      .WillOnce(Return(CpuTimes{false, false, 0, 0, 0}))    // cgroup v1
      .WillOnce(Return(CpuTimes{false, false, 0, 200, 0})); // cgroup v1
  auto monitor = std::make_unique<CpuUtilizationMonitor>(config, std::move(stats_reader));

  ResourcePressure resource;
  monitor->updateResourceUsage(resource);
  ASSERT_TRUE(resource.hasError());

  monitor->updateResourceUsage(resource);
  ASSERT_TRUE(resource.hasError());
}

TEST(ContainerCpuUsageMonitorTest, ComputesCorrectUsageCgroupV2) {
  envoy::extensions::resource_monitors::cpu_utilization::v3::CpuUtilizationConfig config;
  config.set_mode(
      envoy::extensions::resource_monitors::cpu_utilization::v3::CpuUtilizationConfig::CONTAINER);
  auto stats_reader = std::make_unique<MockCpuStatsReader>();
  // Initial sample
  EXPECT_CALL(*stats_reader, getCpuTimes())
      .WillOnce(Return(CpuTimes{true, true, 0, 1000000000ULL, 2}))
      // Second sample after 1 second: 1,000,000 usec consumed across 2 cores -> 0.5
      .WillOnce(Return(CpuTimes{true, true, 1000000, 2000000000ULL, 2}));
  auto monitor = std::make_unique<CpuUtilizationMonitor>(config, std::move(stats_reader));

  ResourcePressure resource;
  monitor->updateResourceUsage(resource);
  ASSERT_TRUE(resource.hasPressure());
  ASSERT_FALSE(resource.hasError());
  // dampened: 0.5 * 0.05 = 0.025
  EXPECT_DOUBLE_EQ(resource.pressure(), 0.025);
}

// Verify calculation works correctly with a single CPU core
TEST(HostCpuUtilizationMonitorTest, CgroupV2SingleCore) {
  envoy::extensions::resource_monitors::cpu_utilization::v3::CpuUtilizationConfig config;
  auto stats_reader = std::make_unique<MockCpuStatsReader>();

  // Scenario: Container with 1 core, using 50% of that core
  // work_time: 50ms = 50000 usec, total_time: 100ms = 100000000 nsec
  // Formula: ((50000 / 1000000.0) / (0.1 * 1.0)) = 0.05 / 0.1 = 0.5 (50% utilization)
  EXPECT_CALL(*stats_reader, getCpuTimes())
      .WillOnce(Return(CpuTimes{true, true, 0.0, 0, 1.0})) // Initial: 0ms work, 0ms total, 1 core
      .WillOnce(Return(
          CpuTimes{true, true, 50000.0, 100000000, 1.0})) // After: 50ms work, 100ms total, 1 core
      .WillOnce(Return(CpuTimes{true, true, 100000.0, 200000000,
                                1.0})); // After: 100ms work, 200ms total, 1 core
  auto monitor = std::make_unique<CpuUtilizationMonitor>(config, std::move(stats_reader));

  ResourcePressure resource;
  // First update: 50% utilization * 0.05 (DAMPENING_ALPHA) = 0.025
  monitor->updateResourceUsage(resource);
  ASSERT_TRUE(resource.hasPressure());
  ASSERT_FALSE(resource.hasError());
  EXPECT_DOUBLE_EQ(resource.pressure(), 0.025);

  // Second update: 50% utilization * 0.05 + 0.025 * 0.95 = 0.04875
  monitor->updateResourceUsage(resource);
  ASSERT_TRUE(resource.hasPressure());
  ASSERT_FALSE(resource.hasError());
  EXPECT_DOUBLE_EQ(resource.pressure(), 0.04875);
}

// Test realistic fractional CPU limits (e.g., 1.5, 0.5 cores = "500m")
TEST(HostCpuUtilizationMonitorTest, CgroupV2FractionalCores) {
  envoy::extensions::resource_monitors::cpu_utilization::v3::CpuUtilizationConfig config;
  auto stats_reader = std::make_unique<MockCpuStatsReader>();

  // Scenario: Container with 1.5 cores (cpu.max = "150000 100000")
  // Using 75ms of CPU work over 100ms period on 1.5 cores
  // Formula: ((75000 / 1000000.0) / (0.1 * 1.5)) = 0.075 / 0.15 = 0.5 (50% utilization)
  EXPECT_CALL(*stats_reader, getCpuTimes())
      .WillOnce(Return(CpuTimes{true, true, 0.0, 0, 1.5}))
      .WillOnce(Return(CpuTimes{true, true, 75000.0, 100000000, 1.5}));
  auto monitor = std::make_unique<CpuUtilizationMonitor>(config, std::move(stats_reader));

  ResourcePressure resource;
  monitor->updateResourceUsage(resource);
  ASSERT_TRUE(resource.hasPressure());
  ASSERT_FALSE(resource.hasError());
  EXPECT_DOUBLE_EQ(resource.pressure(), 0.025); // 0.5 * 0.05
}

// Test with many cores (32 cores) to ensure no overflow issues
// This tests scenarios in large servers with many CPU cores
TEST(HostCpuUtilizationMonitorTest, CgroupV2HighCoreCount) {
  envoy::extensions::resource_monitors::cpu_utilization::v3::CpuUtilizationConfig config;
  auto stats_reader = std::make_unique<MockCpuStatsReader>();

  // Scenario: Large server with 32 cores, using 800ms over 100ms = 8 cores fully utilized
  // Formula: ((800000 / 1000000.0) / (0.1 * 32)) = 0.8 / 3.2 = 0.25 (25% utilization)
  EXPECT_CALL(*stats_reader, getCpuTimes())
      .WillOnce(Return(CpuTimes{true, true, 0.0, 0, 32.0}))
      .WillOnce(Return(CpuTimes{true, true, 800000.0, 100000000, 32.0}));
  auto monitor = std::make_unique<CpuUtilizationMonitor>(config, std::move(stats_reader));

  ResourcePressure resource;
  monitor->updateResourceUsage(resource);
  ASSERT_TRUE(resource.hasPressure());
  ASSERT_FALSE(resource.hasError());
  EXPECT_DOUBLE_EQ(resource.pressure(), 0.0125); // 0.25 * 0.05
}

// Verify clamp(0.0, 100.0) works when calculated value > 100
// This can happen due to timing anomalies or system clock adjustments
TEST(HostCpuUtilizationMonitorTest, CgroupV2ClampingAtMaximum) {
  envoy::extensions::resource_monitors::cpu_utilization::v3::CpuUtilizationConfig config;
  auto stats_reader = std::make_unique<MockCpuStatsReader>();

  // Scenario: Anomalous reading where work_time exceeds what's theoretically possible
  // work_time: 50,000ms (50 seconds!) on 2 cores over 100ms period = 25,000% utilization
  // Formula: ((50000000 / 1000000.0) / (0.1 * 2)) = 50.0 / 0.2 = 250.0 → clamped to 1.0
  // Then: 1.0 * 0.05 (DAMPENING_ALPHA) = 0.05
  EXPECT_CALL(*stats_reader, getCpuTimes())
      .WillOnce(Return(CpuTimes{true, true, 0.0, 0, 2.0}))
      .WillOnce(Return(CpuTimes{true, true, 50000000.0, 100000000, 2.0})); // 50 seconds of work!
  auto monitor = std::make_unique<CpuUtilizationMonitor>(config, std::move(stats_reader));

  ResourcePressure resource;
  monitor->updateResourceUsage(resource);
  ASSERT_TRUE(resource.hasPressure());
  ASSERT_FALSE(resource.hasError());
  // Calculation gives 250.0 (2.5 as fraction), clamped to 1.0, then EWMA: 1.0 * 0.05 = 0.05
  EXPECT_DOUBLE_EQ(resource.pressure(), 0.05);
}

// Test very low CPU usage scenarios (idle container)
// Ensures precision is maintained even with very small values
TEST(HostCpuUtilizationMonitorTest, CgroupV2NearZeroUsage) {
  envoy::extensions::resource_monitors::cpu_utilization::v3::CpuUtilizationConfig config;
  auto stats_reader = std::make_unique<MockCpuStatsReader>();

  // Scenario: Nearly idle container, using 0.1ms over 1000ms on 2 cores
  // Formula: ((100 / 1000000.0) / (1.0 * 2)) = 0.0001 / 2 = 0.00005 (0.005% utilization)
  EXPECT_CALL(*stats_reader, getCpuTimes())
      .WillOnce(Return(CpuTimes{true, true, 0.0, 0, 2.0}))
      .WillOnce(Return(CpuTimes{true, true, 100.0, 1000000000, 2.0}));
  auto monitor = std::make_unique<CpuUtilizationMonitor>(config, std::move(stats_reader));

  ResourcePressure resource;
  monitor->updateResourceUsage(resource);
  ASSERT_TRUE(resource.hasPressure());
  ASSERT_FALSE(resource.hasError());
  EXPECT_DOUBLE_EQ(resource.pressure(), 0.0000025); // 0.00005 * 0.05
}

// Test when no CPU work was done in the period
// This represents a completely idle period
TEST(HostCpuUtilizationMonitorTest, CgroupV2ZeroWorkTime) {
  envoy::extensions::resource_monitors::cpu_utilization::v3::CpuUtilizationConfig config;
  auto stats_reader = std::make_unique<MockCpuStatsReader>();

  // Scenario: No CPU work done (container sleeping)
  // work_over_period = 0, so utilization should be 0%
  EXPECT_CALL(*stats_reader, getCpuTimes())
      .WillOnce(Return(CpuTimes{true, true, 1000.0, 1000000000, 2.0}))
      .WillOnce(Return(CpuTimes{true, true, 1000.0, 2000000000, 2.0})); // Same work_time
  auto monitor = std::make_unique<CpuUtilizationMonitor>(config, std::move(stats_reader));

  ResourcePressure resource;
  monitor->updateResourceUsage(resource);
  ASSERT_TRUE(resource.hasPressure());
  ASSERT_FALSE(resource.hasError());
  EXPECT_DOUBLE_EQ(resource.pressure(), 0.0); // 0 * 0.05 = 0
}

// Test with large time periods (1 hour of uptime)
// Ensures no overflow with large nanosecond values
TEST(HostCpuUtilizationMonitorTest, CgroupV2LargeTimeDelta) {
  envoy::extensions::resource_monitors::cpu_utilization::v3::CpuUtilizationConfig config;
  auto stats_reader = std::make_unique<MockCpuStatsReader>();

  // Scenario: 1 hour period (3600 seconds = 3,600,000,000,000 nanoseconds)
  // Using 1.8 billion usec (1,800,000 ms) on 4 cores = 50% utilization
  // Formula: ((1800000000 / 1000000.0) / (3600 * 4)) = 1800 / 14400 = 0.125
  EXPECT_CALL(*stats_reader, getCpuTimes())
      .WillOnce(Return(CpuTimes{true, true, 0.0, 0, 4.0}))
      .WillOnce(Return(CpuTimes{true, true, 1800000000.0, 3600000000000ULL, 4.0}));
  auto monitor = std::make_unique<CpuUtilizationMonitor>(config, std::move(stats_reader));

  ResourcePressure resource;
  monitor->updateResourceUsage(resource);
  ASSERT_TRUE(resource.hasPressure());
  ASSERT_FALSE(resource.hasError());
  EXPECT_DOUBLE_EQ(resource.pressure(), 0.00625); // 0.125 * 0.05
}

// Test rapid sampling (1ms intervals)
// Simulates high-frequency monitoring
TEST(HostCpuUtilizationMonitorTest, CgroupV2SmallTimeDelta) {
  envoy::extensions::resource_monitors::cpu_utilization::v3::CpuUtilizationConfig config;
  auto stats_reader = std::make_unique<MockCpuStatsReader>();

  // Scenario: 1ms sampling period (1,000,000 nanoseconds)
  // Using 1ms on 2 cores = 50% utilization
  // Formula: ((1000 / 1000000.0) / (0.001 * 2)) = 0.001 / 0.002 = 0.5
  EXPECT_CALL(*stats_reader, getCpuTimes())
      .WillOnce(Return(CpuTimes{true, true, 0.0, 0, 2.0}))
      .WillOnce(Return(CpuTimes{true, true, 1000.0, 1000000, 2.0}));
  auto monitor = std::make_unique<CpuUtilizationMonitor>(config, std::move(stats_reader));

  ResourcePressure resource;
  monitor->updateResourceUsage(resource);
  ASSERT_TRUE(resource.hasPressure());
  ASSERT_FALSE(resource.hasError());
  EXPECT_DOUBLE_EQ(resource.pressure(), 0.025); // 0.5 * 0.05
}

// Verify correct conversion between time units with precise values
// Tests the formula's handling of unit conversions
TEST(HostCpuUtilizationMonitorTest, CgroupV2TimeUnitConversion) {
  envoy::extensions::resource_monitors::cpu_utilization::v3::CpuUtilizationConfig config;
  auto stats_reader = std::make_unique<MockCpuStatsReader>();

  // Scenario: Precise values to test conversion
  // work_time: 123456 usec = 123.456ms, total_time: 500000000 nsec = 500ms, 1 core
  // Formula: ((123456 / 1000000.0) / (0.5 * 1)) = 0.123456 / 0.5 = 0.246912
  EXPECT_CALL(*stats_reader, getCpuTimes())
      .WillOnce(Return(CpuTimes{true, true, 0.0, 0, 1.0}))
      .WillOnce(Return(CpuTimes{true, true, 123456.0, 500000000, 1.0}));
  auto monitor = std::make_unique<CpuUtilizationMonitor>(config, std::move(stats_reader));

  ResourcePressure resource;
  monitor->updateResourceUsage(resource);
  ASSERT_TRUE(resource.hasPressure());
  ASSERT_FALSE(resource.hasError());
  EXPECT_DOUBLE_EQ(resource.pressure(), 0.012345600000000001); // 0.246912 * 0.05
}

// Test rapid changes in CPU usage (alternating high/low)
// Demonstrates EWMA smoothing dampens spikes
TEST(HostCpuUtilizationMonitorTest, CgroupV2BurstyCpuUsage) {
  envoy::extensions::resource_monitors::cpu_utilization::v3::CpuUtilizationConfig config;
  auto stats_reader = std::make_unique<MockCpuStatsReader>();

  // Scenario: Alternating between 10% and 90% utilization on 2 cores
  // Period 1: 10ms work over 100ms = 5% per core = 10% total
  // Period 2: 90ms work over 100ms = 45% per core = 90% total
  // Period 3: 10ms work over 100ms = 5% per core = 10% total
  EXPECT_CALL(*stats_reader, getCpuTimes())
      .WillOnce(Return(CpuTimes{true, true, 0.0, 0, 2.0}))
      .WillOnce(Return(CpuTimes{true, true, 20000.0, 100000000, 2.0}))   // 10% utilization
      .WillOnce(Return(CpuTimes{true, true, 200000.0, 200000000, 2.0}))  // 90% utilization spike
      .WillOnce(Return(CpuTimes{true, true, 220000.0, 300000000, 2.0})); // Back to 10%
  auto monitor = std::make_unique<CpuUtilizationMonitor>(config, std::move(stats_reader));

  ResourcePressure resource;
  // First: 10% * 0.05 = 0.5%
  monitor->updateResourceUsage(resource);
  EXPECT_DOUBLE_EQ(resource.pressure(), 0.005);

  // Second: 90% * 0.05 + 0.005 * 0.95 = 4.5% + 0.475% = 4.975%
  monitor->updateResourceUsage(resource);
  EXPECT_DOUBLE_EQ(resource.pressure(), 0.04975);

  // Third: 10% * 0.05 + 0.04975 * 0.95 = 0.5% + 4.72625% = 5.22625%
  // Shows dampening effect - doesn't drop immediately back to 0.5%
  monitor->updateResourceUsage(resource);
  EXPECT_DOUBLE_EQ(resource.pressure(), 0.0522625);
}

// Test sustained near-100% CPU usage
// Verifies behavior under continuous high load
TEST(HostCpuUtilizationMonitorTest, CgroupV2SustainedHighLoad) {
  envoy::extensions::resource_monitors::cpu_utilization::v3::CpuUtilizationConfig config;
  auto stats_reader = std::make_unique<MockCpuStatsReader>();

  // Scenario: Sustained 95% CPU utilization on 2 cores
  // 190ms work over 100ms on 2 cores = 95% utilization
  EXPECT_CALL(*stats_reader, getCpuTimes())
      .WillOnce(Return(CpuTimes{true, true, 0.0, 0, 2.0}))
      .WillOnce(Return(CpuTimes{true, true, 190000.0, 100000000, 2.0}))
      .WillOnce(Return(CpuTimes{true, true, 380000.0, 200000000, 2.0}))
      .WillOnce(Return(CpuTimes{true, true, 570000.0, 300000000, 2.0}));
  auto monitor = std::make_unique<CpuUtilizationMonitor>(config, std::move(stats_reader));

  ResourcePressure resource;
  // Update 1: 95% * 0.05 = 4.75%
  monitor->updateResourceUsage(resource);
  EXPECT_DOUBLE_EQ(resource.pressure(), 0.0475);

  // Update 2: 95% * 0.05 + 4.75% * 0.95 = 4.75% + 4.5125% = 9.2625%
  monitor->updateResourceUsage(resource);
  EXPECT_DOUBLE_EQ(resource.pressure(), 0.092625);

  // Update 3: Should continue climbing toward 95%
  monitor->updateResourceUsage(resource);
  EXPECT_NEAR(resource.pressure(), 0.1354937, 0.0000001);
}

// Test all cores fully utilized (100% on all cores)
// Represents CPU-bound workload maxing out resources
TEST(HostCpuUtilizationMonitorTest, CgroupV2MultiCoreSaturation) {
  envoy::extensions::resource_monitors::cpu_utilization::v3::CpuUtilizationConfig config;
  auto stats_reader = std::make_unique<MockCpuStatsReader>();

  // Scenario: 4 cores, 100% usage = 400ms work over 100ms
  // Formula: ((400000 / 1000000.0) / (0.1 * 4)) = 0.4 / 0.4 = 1.0 (100%)
  EXPECT_CALL(*stats_reader, getCpuTimes())
      .WillOnce(Return(CpuTimes{true, true, 0.0, 0, 4.0}))
      .WillOnce(Return(CpuTimes{true, true, 400000.0, 100000000, 4.0}));
  auto monitor = std::make_unique<CpuUtilizationMonitor>(config, std::move(stats_reader));

  ResourcePressure resource;
  // 100% * 0.05 = 5.0%
  monitor->updateResourceUsage(resource);
  EXPECT_DOUBLE_EQ(resource.pressure(), 0.05);
}

// Test using only some of available cores
// 4 cores available, but using 2 cores at 100% = 50% overall
TEST(HostCpuUtilizationMonitorTest, CgroupV2PartialCoreUtilization) {
  envoy::extensions::resource_monitors::cpu_utilization::v3::CpuUtilizationConfig config;
  auto stats_reader = std::make_unique<MockCpuStatsReader>();

  // Scenario: 4 cores, but only 2 fully utilized = 200ms work over 100ms
  // Formula: ((200000 / 1000000.0) / (0.1 * 4)) = 0.2 / 0.4 = 0.5 (50%)
  EXPECT_CALL(*stats_reader, getCpuTimes())
      .WillOnce(Return(CpuTimes{true, true, 0.0, 0, 4.0}))
      .WillOnce(Return(CpuTimes{true, true, 200000.0, 100000000, 4.0}));
  auto monitor = std::make_unique<CpuUtilizationMonitor>(config, std::move(stats_reader));

  ResourcePressure resource;
  // 50% * 0.05 = 2.5%
  monitor->updateResourceUsage(resource);
  EXPECT_DOUBLE_EQ(resource.pressure(), 0.025);
}

// Test when work_time goes backwards (negative work_over_period)
// This can happen due to system clock issues or counter resets
TEST(HostCpuUtilizationMonitorTest, CgroupV2TimeRegression) {
  envoy::extensions::resource_monitors::cpu_utilization::v3::CpuUtilizationConfig config;
  auto stats_reader = std::make_unique<MockCpuStatsReader>();

  // Scenario: work_time decreases (goes backwards)
  // This should trigger an error in the monitor
  EXPECT_CALL(*stats_reader, getCpuTimes())
      .WillOnce(Return(CpuTimes{true, true, 100000.0, 100000000, 2.0}))
      .WillOnce(Return(CpuTimes{true, true, 50000.0, 200000000, 2.0})); // work_time decreased!
  auto monitor = std::make_unique<CpuUtilizationMonitor>(config, std::move(stats_reader));

  ResourcePressure resource;
  monitor->updateResourceUsage(resource);
  // work_over_period = 50000 - 100000 = -50000 (negative!)
  // This should trigger an error: "Work_over_period cannot be a negative number"
  ASSERT_TRUE(resource.hasError());
}

// Container with 0.5 cores (500m in Kubernetes)
TEST(ContainerCpuUsageMonitorTest, CgroupV2HalfCore) {
  envoy::extensions::resource_monitors::cpu_utilization::v3::CpuUtilizationConfig config;
  config.set_mode(
      envoy::extensions::resource_monitors::cpu_utilization::v3::CpuUtilizationConfig::CONTAINER);
  auto stats_reader = std::make_unique<MockCpuStatsReader>();

  // Scenario: Container limited to 0.5 cores (cpu.max = "50000 100000")
  // Using 25ms over 100ms = 50% of the 0.5 core allocation
  // Formula: ((25000 / 1000000.0) / (0.1 * 0.5)) = 0.025 / 0.05 = 0.5
  EXPECT_CALL(*stats_reader, getCpuTimes())
      .WillOnce(Return(CpuTimes{true, true, 0.0, 0, 0.5}))
      .WillOnce(Return(CpuTimes{true, true, 25000.0, 100000000, 0.5}));
  auto monitor = std::make_unique<CpuUtilizationMonitor>(config, std::move(stats_reader));

  ResourcePressure resource;
  monitor->updateResourceUsage(resource);
  ASSERT_TRUE(resource.hasPressure());
  ASSERT_FALSE(resource.hasError());
  EXPECT_DOUBLE_EQ(resource.pressure(), 0.025); // 0.5 * 0.05
}

// Container with 0.25 cores (250m in Kubernetes)
TEST(ContainerCpuUsageMonitorTest, CgroupV2QuarterCore) {
  envoy::extensions::resource_monitors::cpu_utilization::v3::CpuUtilizationConfig config;
  config.set_mode(
      envoy::extensions::resource_monitors::cpu_utilization::v3::CpuUtilizationConfig::CONTAINER);
  auto stats_reader = std::make_unique<MockCpuStatsReader>();

  // Scenario: Container limited to 0.25 cores (cpu.max = "25000 100000")
  // Using 25ms over 100ms = 100% of the 0.25 core allocation
  // Formula: ((25000 / 1000000.0) / (0.1 * 0.25)) = 0.025 / 0.025 = 1.0
  EXPECT_CALL(*stats_reader, getCpuTimes())
      .WillOnce(Return(CpuTimes{true, true, 0.0, 0, 0.25}))
      .WillOnce(Return(CpuTimes{true, true, 25000.0, 100000000, 0.25}));
  auto monitor = std::make_unique<CpuUtilizationMonitor>(config, std::move(stats_reader));

  ResourcePressure resource;
  monitor->updateResourceUsage(resource);
  ASSERT_TRUE(resource.hasPressure());
  ASSERT_FALSE(resource.hasError());
  EXPECT_DOUBLE_EQ(resource.pressure(), 0.05); // 1.0 * 0.05
}

// Container with 3.5 cores
TEST(ContainerCpuUsageMonitorTest, CgroupV2ThreeAndHalfCores) {
  envoy::extensions::resource_monitors::cpu_utilization::v3::CpuUtilizationConfig config;
  config.set_mode(
      envoy::extensions::resource_monitors::cpu_utilization::v3::CpuUtilizationConfig::CONTAINER);
  auto stats_reader = std::make_unique<MockCpuStatsReader>();

  // Scenario: Container with 3.5 cores, using 175ms over 100ms = 50% utilization
  // Formula: ((175000 / 1000000.0) / (0.1 * 3.5)) = 0.175 / 0.35 = 0.5
  EXPECT_CALL(*stats_reader, getCpuTimes())
      .WillOnce(Return(CpuTimes{true, true, 0.0, 0, 3.5}))
      .WillOnce(Return(CpuTimes{true, true, 175000.0, 100000000, 3.5}));
  auto monitor = std::make_unique<CpuUtilizationMonitor>(config, std::move(stats_reader));

  ResourcePressure resource;
  monitor->updateResourceUsage(resource);
  ASSERT_TRUE(resource.hasPressure());
  ASSERT_FALSE(resource.hasError());
  EXPECT_DOUBLE_EQ(resource.pressure(), 0.025); // 0.5 * 0.05
}

// Container with NO CPU limit (cpu.max = "max")
// In this case, effective_cores = N (number of CPUs from cpuset.cpus.effective)
TEST(ContainerCpuUsageMonitorTest, CgroupV2NoCpuLimit) {
  envoy::extensions::resource_monitors::cpu_utilization::v3::CpuUtilizationConfig config;
  config.set_mode(
      envoy::extensions::resource_monitors::cpu_utilization::v3::CpuUtilizationConfig::CONTAINER);
  auto stats_reader = std::make_unique<MockCpuStatsReader>();

  // Scenario: No CPU limit set, so container can use all 8 cores from cpuset
  // Using 400ms over 100ms = 50% of 8 cores = 4 cores fully utilized
  // Formula: ((400000 / 1000000.0) / (0.1 * 8)) = 0.4 / 0.8 = 0.5
  EXPECT_CALL(*stats_reader, getCpuTimes())
      .WillOnce(Return(CpuTimes{true, true, 0.0, 0, 8.0})) // effective_cores = N from cpuset
      .WillOnce(Return(CpuTimes{true, true, 400000.0, 100000000, 8.0}));
  auto monitor = std::make_unique<CpuUtilizationMonitor>(config, std::move(stats_reader));

  ResourcePressure resource;
  monitor->updateResourceUsage(resource);
  ASSERT_TRUE(resource.hasPressure());
  ASSERT_FALSE(resource.hasError());
  EXPECT_DOUBLE_EQ(resource.pressure(), 0.025); // 0.5 * 0.05
}

// Container CPU Throttling Scenario
// When container hits its CPU quota limit and gets throttled
// Represents a container trying to use more CPU than allocated
TEST(ContainerCpuUsageMonitorTest, CgroupV2ThrottledContainer) {
  envoy::extensions::resource_monitors::cpu_utilization::v3::CpuUtilizationConfig config;
  config.set_mode(
      envoy::extensions::resource_monitors::cpu_utilization::v3::CpuUtilizationConfig::CONTAINER);
  auto stats_reader = std::make_unique<MockCpuStatsReader>();

  // Scenario: Container limited to 2 cores but trying to use more
  // CPU work approaches the limit: 195ms, 198ms, 199ms over 100ms periods
  // This simulates throttling kicking in as utilization approaches 100%
  EXPECT_CALL(*stats_reader, getCpuTimes())
      .WillOnce(Return(CpuTimes{true, true, 0.0, 0, 2.0}))
      .WillOnce(Return(CpuTimes{true, true, 195000.0, 100000000, 2.0}))  // 97.5% utilization
      .WillOnce(Return(CpuTimes{true, true, 393000.0, 200000000, 2.0}))  // 99% utilization
      .WillOnce(Return(CpuTimes{true, true, 592000.0, 300000000, 2.0})); // 99.5% utilization
  auto monitor = std::make_unique<CpuUtilizationMonitor>(config, std::move(stats_reader));

  ResourcePressure resource;
  // First: 97.5% * 0.05 = 4.875%
  monitor->updateResourceUsage(resource);
  EXPECT_DOUBLE_EQ(resource.pressure(), 0.04875);

  // Second: 99% * 0.05 + 4.875% * 0.95 = 4.95% + 4.63125% = 9.58125%
  monitor->updateResourceUsage(resource);
  EXPECT_DOUBLE_EQ(resource.pressure(), 0.0958125);

  // Third: Continues climbing as throttling occurs
  monitor->updateResourceUsage(resource);
  EXPECT_NEAR(resource.pressure(), 0.1407719, 0.0001);
}

// Container with Large Core Range (16-core system)
TEST(ContainerCpuUsageMonitorTest, CgroupV2LargeCoreRange) {
  envoy::extensions::resource_monitors::cpu_utilization::v3::CpuUtilizationConfig config;
  config.set_mode(
      envoy::extensions::resource_monitors::cpu_utilization::v3::CpuUtilizationConfig::CONTAINER);
  auto stats_reader = std::make_unique<MockCpuStatsReader>();

  // Scenario: Container on 16-core system, limited to 12 cores
  // Using 600ms over 100ms = 50% of 12 cores = 6 cores fully utilized
  // Formula: ((600000 / 1000000.0) / (0.1 * 12)) = 0.6 / 1.2 = 0.5
  EXPECT_CALL(*stats_reader, getCpuTimes())
      .WillOnce(Return(CpuTimes{true, true, 0.0, 0, 12.0}))
      .WillOnce(Return(CpuTimes{true, true, 600000.0, 100000000, 12.0}));
  auto monitor = std::make_unique<CpuUtilizationMonitor>(config, std::move(stats_reader));

  ResourcePressure resource;
  monitor->updateResourceUsage(resource);
  ASSERT_TRUE(resource.hasPressure());
  ASSERT_FALSE(resource.hasError());
  EXPECT_DOUBLE_EQ(resource.pressure(), 0.025); // 0.5 * 0.05
}

// Dynamic Core Changes (cores available changes between samples)
// If effective_cores changes mid-monitoring
// This can happen with CPU hotplug or cgroup limit updates
TEST(ContainerCpuUsageMonitorTest, CgroupV2DynamicCoreChange) {
  envoy::extensions::resource_monitors::cpu_utilization::v3::CpuUtilizationConfig config;
  config.set_mode(
      envoy::extensions::resource_monitors::cpu_utilization::v3::CpuUtilizationConfig::CONTAINER);
  auto stats_reader = std::make_unique<MockCpuStatsReader>();

  // Scenario: Core count changes from 4 to 2 (limit reduced)
  // This tests if monitor handles changing effective_cores gracefully
  EXPECT_CALL(*stats_reader, getCpuTimes())
      .WillOnce(Return(CpuTimes{true, true, 0.0, 0, 4.0}))
      .WillOnce(Return(CpuTimes{true, true, 200000.0, 100000000, 4.0})) // 50% on 4 cores
      .WillOnce(
          Return(CpuTimes{true, true, 300000.0, 200000000, 2.0})); // Now only 2 cores available!
  auto monitor = std::make_unique<CpuUtilizationMonitor>(config, std::move(stats_reader));

  ResourcePressure resource;
  // First: 50% utilization on 4 cores
  monitor->updateResourceUsage(resource);
  EXPECT_DOUBLE_EQ(resource.pressure(), 0.025); // 0.5 * 0.05

  // Second: 100ms work over 100ms with 2 cores = 50% utilization
  // Formula: ((100000 / 1000000.0) / (0.1 * 2)) = 0.1 / 0.2 = 0.5
  monitor->updateResourceUsage(resource);
  EXPECT_DOUBLE_EQ(resource.pressure(), 0.04875); // 0.5 * 0.05 + 0.025 * 0.95
}

} // namespace
} // namespace CpuUtilizationMonitor
} // namespace ResourceMonitors
} // namespace Extensions
} // namespace Envoy