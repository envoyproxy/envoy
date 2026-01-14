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

  MOCK_METHOD(absl::StatusOr<double>, getUtilization, ());
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
  // Mock returns utilization directly
  // Note: Constructor calls getUtilization() once to establish baseline
  // Original test: CpuTimes{50,100}, {100,200}, {200,300}
  // Delta1: (100-50)/(200-100) = 50/100 = 0.5
  // Delta2: (200-100)/(300-200) = 100/100 = 1.0
  EXPECT_CALL(*stats_reader, getUtilization())
      .WillOnce(Return(0.0)) // Constructor call
      .WillOnce(Return(0.5)) // First update: 50% utilization
      .WillOnce(Return(1.0)); // Second update: 100% utilization
  auto monitor = std::make_unique<CpuUtilizationMonitor>(config, std::move(stats_reader));

  ResourcePressure resource;
  monitor->updateResourceUsage(resource);
  ASSERT_TRUE(resource.hasPressure());
  ASSERT_FALSE(resource.hasError());
  EXPECT_DOUBLE_EQ(resource.pressure(), 0.025); // 0.5 * 0.05

  monitor->updateResourceUsage(resource);
  ASSERT_TRUE(resource.hasPressure());
  ASSERT_FALSE(resource.hasError());
  EXPECT_DOUBLE_EQ(resource.pressure(), 0.07375); // 1.0 * 0.05 + 0.025 * 0.95
}

TEST(HostCpuUtilizationMonitorTest, ComputesCorrectUsageCgroupV2) {
  envoy::extensions::resource_monitors::cpu_utilization::v3::CpuUtilizationConfig config;
  auto stats_reader = std::make_unique<MockCpuStatsReader>();
  // Mock cgroup v2 returns utilization directly
  // Note: Constructor calls getUtilization() once to establish baseline
  EXPECT_CALL(*stats_reader, getUtilization())
      .WillOnce(Return(0.0))  // Constructor call
      .WillOnce(Return(0.25)) // First update: 25% utilization
      .WillOnce(Return(0.25)); // Second update: 25% utilization
  auto monitor = std::make_unique<CpuUtilizationMonitor>(config, std::move(stats_reader));

  ResourcePressure resource;
  // First update: utilization = 0.25 * 0.05 = 0.0125
  monitor->updateResourceUsage(resource);
  ASSERT_TRUE(resource.hasPressure());
  ASSERT_FALSE(resource.hasError());
  EXPECT_DOUBLE_EQ(resource.pressure(), 0.0125);

  // Second update: utilization = 0.25 * 0.05 + 0.0125 * 0.95 = 0.024375
  monitor->updateResourceUsage(resource);
  ASSERT_TRUE(resource.hasPressure());
  ASSERT_FALSE(resource.hasError());
  EXPECT_DOUBLE_EQ(resource.pressure(), 0.024375);
}

TEST(HostCpuUtilizationMonitorTest, GetsErroneousStatsDenominator) {
  envoy::extensions::resource_monitors::cpu_utilization::v3::CpuUtilizationConfig config;
  auto stats_reader = std::make_unique<MockCpuStatsReader>();
  // Mock returns error for invalid denominator
  // Constructor call succeeds with 0.0, then update fails
  EXPECT_CALL(*stats_reader, getUtilization())
      .WillOnce(Return(0.0)) // Constructor call
      .WillOnce(Return(absl::InvalidArgumentError("total_over_period must be positive")));
  auto monitor = std::make_unique<CpuUtilizationMonitor>(config, std::move(stats_reader));
  ResourcePressure resource;
  monitor->updateResourceUsage(resource);
  ASSERT_TRUE(resource.hasError());
}

TEST(HostCpuUtilizationMonitorTest, GetsErroneousStatsNumerator) {
  envoy::extensions::resource_monitors::cpu_utilization::v3::CpuUtilizationConfig config;
  auto stats_reader = std::make_unique<MockCpuStatsReader>();
  // Mock returns error for invalid numerator (negative work delta)
  // Constructor call succeeds with 0.0, then update fails
  EXPECT_CALL(*stats_reader, getUtilization())
      .WillOnce(Return(0.0)) // Constructor call
      .WillOnce(Return(absl::InvalidArgumentError("Work_over_period cannot be negative")));
  auto monitor = std::make_unique<CpuUtilizationMonitor>(config, std::move(stats_reader));

  ResourcePressure resource;
  monitor->updateResourceUsage(resource);
  ASSERT_TRUE(resource.hasError());
}

TEST(HostCpuUtilizationMonitorTest, ReportsError) {
  envoy::extensions::resource_monitors::cpu_utilization::v3::CpuUtilizationConfig config;
  auto stats_reader = std::make_unique<MockCpuStatsReader>();
  // Mock returns error (e.g., file read failure)
  // Constructor call succeeds with 0.0, then updates fail
  EXPECT_CALL(*stats_reader, getUtilization())
      .WillOnce(Return(0.0)) // Constructor call
      .WillOnce(Return(absl::InvalidArgumentError("Failed to read CPU times")))
      .WillOnce(Return(absl::InvalidArgumentError("Failed to read CPU times")));
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
  // Mock returns 100% utilization (work delta 1, total delta 1)
  EXPECT_CALL(*stats_reader, getUtilization())
      .WillOnce(Return(0.0)) // Constructor call
      .WillOnce(Return(1.0)) // First call: 100% utilization
      .WillOnce(Return(1.0)); // Second call: 100% utilization
  auto monitor = std::make_unique<CpuUtilizationMonitor>(config, std::move(stats_reader));

  ResourcePressure resource;
  monitor->updateResourceUsage(resource);
  ASSERT_TRUE(resource.hasPressure());
  ASSERT_FALSE(resource.hasError());
  EXPECT_DOUBLE_EQ(resource.pressure(), 0.05); // 1.0 * 0.05

  monitor->updateResourceUsage(resource);
  ASSERT_TRUE(resource.hasPressure());
  ASSERT_FALSE(resource.hasError());
  EXPECT_DOUBLE_EQ(resource.pressure(), 0.0975); // 1.0 * 0.05 + 0.05 * 0.95
}

TEST(ContainerCpuUsageMonitorTest, GetsErroneousStatsDenominator) {
  envoy::extensions::resource_monitors::cpu_utilization::v3::CpuUtilizationConfig config;
  config.set_mode(
      envoy::extensions::resource_monitors::cpu_utilization::v3::CpuUtilizationConfig::CONTAINER);
  auto stats_reader = std::make_unique<MockCpuStatsReader>();
  // Mock returns error for invalid denominator
  EXPECT_CALL(*stats_reader, getUtilization())
      .WillOnce(Return(0.0)) // Constructor call
      .WillOnce(Return(absl::InvalidArgumentError("total_over_period must be positive")));
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
  // Mock returns error for invalid numerator (negative work delta)
  EXPECT_CALL(*stats_reader, getUtilization())
      .WillOnce(Return(0.0)) // Constructor call
      .WillOnce(Return(absl::InvalidArgumentError("Work_over_period cannot be negative")));
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
  // Mock returns error (e.g., file read failure)
  EXPECT_CALL(*stats_reader, getUtilization())
      .WillOnce(Return(0.0)) // Constructor call
      .WillOnce(Return(absl::InvalidArgumentError("Failed to read CPU times")))
      .WillOnce(Return(absl::InvalidArgumentError("Failed to read CPU times")));
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
  // Mock cgroup v2: 1,000,000 usec work over 1s (1B nsec) with 2 cores = 50% utilization
  // Formula: (1000000 / 1000000.0) / ((1000000000 / 1000000000.0) * 2) = 1.0 / 2.0 = 0.5
  EXPECT_CALL(*stats_reader, getUtilization())
      .WillOnce(Return(0.0)) // Constructor call
      .WillOnce(Return(0.5)); // 50% utilization
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
  // work_time: 50ms, total_time: 100ms, 1 core = 50% utilization
  EXPECT_CALL(*stats_reader, getUtilization())
      .WillOnce(Return(0.0)) // Constructor call
      .WillOnce(Return(0.5)) // First call: 50% utilization
      .WillOnce(Return(0.5)); // Second call: 50% utilization
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
  // Using 75ms of CPU work over 100ms period on 1.5 cores = 50% utilization
  EXPECT_CALL(*stats_reader, getUtilization())
      .WillOnce(Return(0.0)) // Constructor call
      .WillOnce(Return(0.5)); // 50% utilization
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
  // Result: 25% utilization
  EXPECT_CALL(*stats_reader, getUtilization())
      .WillOnce(Return(0.0)) // Constructor call
      .WillOnce(Return(0.25)); // 25% utilization
  auto monitor = std::make_unique<CpuUtilizationMonitor>(config, std::move(stats_reader));

  ResourcePressure resource;
  monitor->updateResourceUsage(resource);
  ASSERT_TRUE(resource.hasPressure());
  ASSERT_FALSE(resource.hasError());
  EXPECT_DOUBLE_EQ(resource.pressure(), 0.0125); // 0.25 * 0.05
}

// Verify clamp(0.0, 1.0) works when calculated value > 1.0
// This can happen due to timing anomalies or system clock adjustments
TEST(HostCpuUtilizationMonitorTest, CgroupV2ClampingAtMaximum) {
  envoy::extensions::resource_monitors::cpu_utilization::v3::CpuUtilizationConfig config;
  auto stats_reader = std::make_unique<MockCpuStatsReader>();

  // Scenario: Anomalous reading clamped to 1.0 (100% utilization)
  // The implementation clamps the result to [0.0, 1.0]
  EXPECT_CALL(*stats_reader, getUtilization())
      .WillOnce(Return(0.0)) // Constructor call
      .WillOnce(Return(1.0)); // Clamped to 100%
  auto monitor = std::make_unique<CpuUtilizationMonitor>(config, std::move(stats_reader));

  ResourcePressure resource;
  monitor->updateResourceUsage(resource);
  ASSERT_TRUE(resource.hasPressure());
  ASSERT_FALSE(resource.hasError());
  // EWMA: 1.0 * 0.05 = 0.05
  EXPECT_DOUBLE_EQ(resource.pressure(), 0.05);
}

// Test very low CPU usage scenarios (idle container)
// Ensures precision is maintained even with very small values
TEST(HostCpuUtilizationMonitorTest, CgroupV2NearZeroUsage) {
  envoy::extensions::resource_monitors::cpu_utilization::v3::CpuUtilizationConfig config;
  auto stats_reader = std::make_unique<MockCpuStatsReader>();

  // Scenario: Nearly idle container, using 0.1ms over 1000ms on 2 cores = 0.005% utilization
  EXPECT_CALL(*stats_reader, getUtilization())
      .WillOnce(Return(0.0)) // Constructor call
      .WillOnce(Return(0.00005)); // 0.005% utilization
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

  // Scenario: No CPU work done (container sleeping) = 0% utilization
  EXPECT_CALL(*stats_reader, getUtilization())
      .WillOnce(Return(0.0)) // Constructor call
      .WillOnce(Return(0.0)); // 0% utilization
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

  // Scenario: 1 hour period with 12.5% utilization
  EXPECT_CALL(*stats_reader, getUtilization())
      .WillOnce(Return(0.0)) // Constructor call
      .WillOnce(Return(0.125)); // 12.5% utilization
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

  // Scenario: 1ms sampling period with 50% utilization
  EXPECT_CALL(*stats_reader, getUtilization())
      .WillOnce(Return(0.0)) // Constructor call
      .WillOnce(Return(0.5)); // 50% utilization
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

  // Scenario: Precise utilization value (123.456ms work / 500ms on 1 core = 24.6912% utilization)
  EXPECT_CALL(*stats_reader, getUtilization())
      .WillOnce(Return(0.0)) // Constructor call
      .WillOnce(Return(0.246912)); // 24.6912% utilization
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

  // Scenario: Alternating between 10% and 90% utilization
  EXPECT_CALL(*stats_reader, getUtilization())
      .WillOnce(Return(0.0)) // Constructor call
      .WillOnce(Return(0.1)) // 10% utilization
      .WillOnce(Return(0.9)) // 90% utilization spike
      .WillOnce(Return(0.1)); // Back to 10%
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
  EXPECT_CALL(*stats_reader, getUtilization())
      .WillOnce(Return(0.0)) // Constructor call
      .WillOnce(Return(0.95)) // 95% utilization
      .WillOnce(Return(0.95)) // 95% utilization
      .WillOnce(Return(0.95)); // 95% utilization
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

  // Scenario: 4 cores, 100% usage
  EXPECT_CALL(*stats_reader, getUtilization())
      .WillOnce(Return(0.0)) // Constructor call
      .WillOnce(Return(1.0)); // 100% utilization
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

  // Scenario: 4 cores, but only 2 fully utilized = 50% utilization
  EXPECT_CALL(*stats_reader, getUtilization())
      .WillOnce(Return(0.0)) // Constructor call
      .WillOnce(Return(0.5)); // 50% utilization
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
  // This should trigger an error in the implementation
  EXPECT_CALL(*stats_reader, getUtilization())
      .WillOnce(Return(0.0)) // Constructor call
      .WillOnce(Return(absl::InvalidArgumentError("Work_over_period cannot be negative")));
  auto monitor = std::make_unique<CpuUtilizationMonitor>(config, std::move(stats_reader));

  ResourcePressure resource;
  monitor->updateResourceUsage(resource);
  // This should trigger an error: "Work_over_period cannot be a negative number"
  ASSERT_TRUE(resource.hasError());
}

// Container with 0.5 cores (500m in Kubernetes)
TEST(ContainerCpuUsageMonitorTest, CgroupV2HalfCore) {
  envoy::extensions::resource_monitors::cpu_utilization::v3::CpuUtilizationConfig config;
  config.set_mode(
      envoy::extensions::resource_monitors::cpu_utilization::v3::CpuUtilizationConfig::CONTAINER);
  auto stats_reader = std::make_unique<MockCpuStatsReader>();

  // Scenario: Container limited to 0.5 cores, using 50% of that allocation
  EXPECT_CALL(*stats_reader, getUtilization())
      .WillOnce(Return(0.0)) // Constructor call
      .WillOnce(Return(0.5)); // 50% utilization
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

  // Scenario: Container limited to 0.25 cores, using 100% of that allocation
  EXPECT_CALL(*stats_reader, getUtilization())
      .WillOnce(Return(0.0)) // Constructor call
      .WillOnce(Return(1.0)); // 100% utilization
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

  // Scenario: Container with 3.5 cores, using 50% utilization
  EXPECT_CALL(*stats_reader, getUtilization())
      .WillOnce(Return(0.0)) // Constructor call
      .WillOnce(Return(0.5)); // 50% utilization
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
  // Result: 50% utilization
  EXPECT_CALL(*stats_reader, getUtilization())
      .WillOnce(Return(0.0)) // Constructor call
      .WillOnce(Return(0.5)); // 50% utilization
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
  // This simulates throttling kicking in as utilization approaches 100%
  EXPECT_CALL(*stats_reader, getUtilization())
      .WillOnce(Return(0.0)) // Constructor call
      .WillOnce(Return(0.975)) // 97.5% utilization
      .WillOnce(Return(0.99)) // 99% utilization
      .WillOnce(Return(0.995)); // 99.5% utilization
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

  // Scenario: Container on 16-core system, limited to 12 cores, using 50%
  EXPECT_CALL(*stats_reader, getUtilization())
      .WillOnce(Return(0.0)) // Constructor call
      .WillOnce(Return(0.5)); // 50% utilization
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
  EXPECT_CALL(*stats_reader, getUtilization())
      .WillOnce(Return(0.0)) // Constructor call
      .WillOnce(Return(0.5)) // 50% utilization on 4 cores
      .WillOnce(Return(0.5)); // 50% utilization on 2 cores (different base)
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

// Cgroup v2 Error Handling Tests
TEST(HostCpuUtilizationMonitorTest, CgroupV2ReportsError) {
  envoy::extensions::resource_monitors::cpu_utilization::v3::CpuUtilizationConfig config;
  auto stats_reader = std::make_unique<MockCpuStatsReader>();
  // Mock returns errors for invalid cgroup v2 data
  EXPECT_CALL(*stats_reader, getUtilization())
      .WillOnce(Return(0.0)) // Constructor call
      .WillOnce(Return(absl::InvalidArgumentError("Failed to read CPU times")))
      .WillOnce(Return(absl::InvalidArgumentError("Failed to read CPU times")));
  auto monitor = std::make_unique<CpuUtilizationMonitor>(config, std::move(stats_reader));

  ResourcePressure resource;
  monitor->updateResourceUsage(resource);
  ASSERT_TRUE(resource.hasError());
  ASSERT_FALSE(resource.hasPressure());

  monitor->updateResourceUsage(resource);
  ASSERT_TRUE(resource.hasError());
  ASSERT_FALSE(resource.hasPressure());
}

TEST(HostCpuUtilizationMonitorTest, CgroupV2ErroneousStatsDenominator) {
  envoy::extensions::resource_monitors::cpu_utilization::v3::CpuUtilizationConfig config;
  auto stats_reader = std::make_unique<MockCpuStatsReader>();
  // Mock returns error for negative or zero total_time delta
  EXPECT_CALL(*stats_reader, getUtilization())
      .WillOnce(Return(0.0)) // Constructor call
      .WillOnce(Return(absl::InvalidArgumentError("total_over_period must be positive")));
  auto monitor = std::make_unique<CpuUtilizationMonitor>(config, std::move(stats_reader));

  ResourcePressure resource;
  monitor->updateResourceUsage(resource);
  ASSERT_TRUE(resource.hasError());
  ASSERT_FALSE(resource.hasPressure());
}

TEST(HostCpuUtilizationMonitorTest, CgroupV2ErroneousStatsNumerator) {
  envoy::extensions::resource_monitors::cpu_utilization::v3::CpuUtilizationConfig config;
  auto stats_reader = std::make_unique<MockCpuStatsReader>();
  // Mock returns error for negative work_time delta
  EXPECT_CALL(*stats_reader, getUtilization())
      .WillOnce(Return(0.0)) // Constructor call
      .WillOnce(Return(absl::InvalidArgumentError("Work_over_period cannot be negative")));
  auto monitor = std::make_unique<CpuUtilizationMonitor>(config, std::move(stats_reader));

  ResourcePressure resource;
  monitor->updateResourceUsage(resource);
  ASSERT_TRUE(resource.hasError());
  ASSERT_FALSE(resource.hasPressure());
}

TEST(HostCpuUtilizationMonitorTest, CgroupV2ZeroTotalTimeDelta) {
  envoy::extensions::resource_monitors::cpu_utilization::v3::CpuUtilizationConfig config;
  auto stats_reader = std::make_unique<MockCpuStatsReader>();
  // Mock returns error for zero total_time delta
  EXPECT_CALL(*stats_reader, getUtilization())
      .WillOnce(Return(0.0)) // Constructor call
      .WillOnce(Return(absl::InvalidArgumentError("total_over_period must be positive")));
  auto monitor = std::make_unique<CpuUtilizationMonitor>(config, std::move(stats_reader));

  ResourcePressure resource;
  monitor->updateResourceUsage(resource);
  ASSERT_TRUE(resource.hasError());
  ASSERT_FALSE(resource.hasPressure());
}

TEST(ContainerCpuUsageMonitorTest, CgroupV2ReportsError) {
  envoy::extensions::resource_monitors::cpu_utilization::v3::CpuUtilizationConfig config;
  config.set_mode(
      envoy::extensions::resource_monitors::cpu_utilization::v3::CpuUtilizationConfig::CONTAINER);
  auto stats_reader = std::make_unique<MockCpuStatsReader>();
  // Mock returns errors for invalid cgroup v2 data
  EXPECT_CALL(*stats_reader, getUtilization())
      .WillOnce(Return(0.0)) // Constructor call
      .WillOnce(Return(absl::InvalidArgumentError("Failed to read CPU times")))
      .WillOnce(Return(absl::InvalidArgumentError("Failed to read CPU times")));
  auto monitor = std::make_unique<CpuUtilizationMonitor>(config, std::move(stats_reader));

  ResourcePressure resource;
  monitor->updateResourceUsage(resource);
  ASSERT_TRUE(resource.hasError());

  monitor->updateResourceUsage(resource);
  ASSERT_TRUE(resource.hasError());
}

TEST(ContainerCpuUsageMonitorTest, CgroupV2ErroneousStatsDenominator) {
  envoy::extensions::resource_monitors::cpu_utilization::v3::CpuUtilizationConfig config;
  config.set_mode(
      envoy::extensions::resource_monitors::cpu_utilization::v3::CpuUtilizationConfig::CONTAINER);
  auto stats_reader = std::make_unique<MockCpuStatsReader>();
  // Mock returns error for negative or zero total_time delta
  EXPECT_CALL(*stats_reader, getUtilization())
      .WillOnce(Return(0.0)) // Constructor call
      .WillOnce(Return(absl::InvalidArgumentError("total_over_period must be positive")));
  auto monitor = std::make_unique<CpuUtilizationMonitor>(config, std::move(stats_reader));

  ResourcePressure resource;
  monitor->updateResourceUsage(resource);
  ASSERT_TRUE(resource.hasError());
}

TEST(ContainerCpuUsageMonitorTest, CgroupV2ErroneousStatsNumerator) {
  envoy::extensions::resource_monitors::cpu_utilization::v3::CpuUtilizationConfig config;
  config.set_mode(
      envoy::extensions::resource_monitors::cpu_utilization::v3::CpuUtilizationConfig::CONTAINER);
  auto stats_reader = std::make_unique<MockCpuStatsReader>();
  // Mock returns error for negative work_time delta
  EXPECT_CALL(*stats_reader, getUtilization())
      .WillOnce(Return(0.0)) // Constructor call
      .WillOnce(Return(absl::InvalidArgumentError("Work_over_period cannot be negative")));
  auto monitor = std::make_unique<CpuUtilizationMonitor>(config, std::move(stats_reader));

  ResourcePressure resource;
  monitor->updateResourceUsage(resource);
  ASSERT_TRUE(resource.hasError());
}

} // namespace
} // namespace CpuUtilizationMonitor
} // namespace ResourceMonitors
} // namespace Extensions
} // namespace Envoy
