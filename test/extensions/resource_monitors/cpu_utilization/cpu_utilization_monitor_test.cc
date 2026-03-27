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

// =============================================================================
// Host CPU Utilization Monitor Tests
// =============================================================================

TEST(HostCpuUtilizationMonitorTest, ComputesCorrectUsage) {
  envoy::extensions::resource_monitors::cpu_utilization::v3::CpuUtilizationConfig config;
  auto stats_reader = std::make_unique<MockCpuStatsReader>();
  // Constructor calls getUtilization() once to establish baseline
  // Then we test EWMA: new = current * 0.05 + previous * 0.95
  EXPECT_CALL(*stats_reader, getUtilization())
      .WillOnce(Return(0.0))  // Constructor call
      .WillOnce(Return(0.5))  // First update: 50% utilization
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

TEST(HostCpuUtilizationMonitorTest, ReportsError) {
  envoy::extensions::resource_monitors::cpu_utilization::v3::CpuUtilizationConfig config;
  auto stats_reader = std::make_unique<MockCpuStatsReader>();
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

// =============================================================================
// Container CPU Utilization Monitor Tests
// =============================================================================

TEST(ContainerCpuUsageMonitorTest, ComputesCorrectUsage) {
  envoy::extensions::resource_monitors::cpu_utilization::v3::CpuUtilizationConfig config;
  config.set_mode(
      envoy::extensions::resource_monitors::cpu_utilization::v3::CpuUtilizationConfig::CONTAINER);
  auto stats_reader = std::make_unique<MockCpuStatsReader>();
  EXPECT_CALL(*stats_reader, getUtilization())
      .WillOnce(Return(0.0))  // Constructor call
      .WillOnce(Return(1.0))  // First call: 100% utilization
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

TEST(ContainerCpuUtilizationMonitorTest, ReportsError) {
  envoy::extensions::resource_monitors::cpu_utilization::v3::CpuUtilizationConfig config;
  config.set_mode(
      envoy::extensions::resource_monitors::cpu_utilization::v3::CpuUtilizationConfig::CONTAINER);
  auto stats_reader = std::make_unique<MockCpuStatsReader>();
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

// =============================================================================
// EWMA Behavior Test - Verifies dampening effect on bursty CPU usage
// =============================================================================

TEST(HostCpuUtilizationMonitorTest, EWMADampensBurstyCpuUsage) {
  envoy::extensions::resource_monitors::cpu_utilization::v3::CpuUtilizationConfig config;
  auto stats_reader = std::make_unique<MockCpuStatsReader>();

  // Scenario: Alternating between low and high utilization
  EXPECT_CALL(*stats_reader, getUtilization())
      .WillOnce(Return(0.0))  // Constructor call
      .WillOnce(Return(0.1))  // 10% utilization
      .WillOnce(Return(0.9))  // 90% utilization spike
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

} // namespace
} // namespace CpuUtilizationMonitor
} // namespace ResourceMonitors
} // namespace Extensions
} // namespace Envoy
