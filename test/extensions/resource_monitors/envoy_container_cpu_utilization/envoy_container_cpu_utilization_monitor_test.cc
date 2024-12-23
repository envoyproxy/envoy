#include <cstdlib>
#include <chrono>

#include "envoy/extensions/resource_monitors/envoy_container_cpu_utilization/v3/envoy_container_cpu_utilization.pb.h"

#include "source/extensions/resource_monitors/envoy_container_cpu_utilization/container_stats_reader.h"
#include "source/extensions/resource_monitors/envoy_container_cpu_utilization/envoy_container_cpu_utilization_monitor.h"

#include "absl/types/optional.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace ResourceMonitors {
namespace EnvoyContainerCpuUtilizationMonitor {
namespace {

using testing::Return;

class MockContainerStatsReader : public ContainerStatsReader {
public:
  MockContainerStatsReader() = default;

  MOCK_METHOD(EnvoyContainerStats, getEnvoyContainerStats, ());
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

// std::chrono::system_clock::time_point time_point_ns = 
//         std::chrono::system_clock::time_point(std::chrono::milliseconds(epoch_ns));

//         1734711389000
TEST(EnvoyContainerCpuUtilizationMonitorTest, ComputesCorrectUsage) {
  envoy::extensions::resource_monitors::envoy_container_cpu_utilization::v3::EnvoyContainerCpuUtilizationConfig config;
  auto stats_reader = std::make_unique<MockContainerStatsReader>();
  EXPECT_CALL(*stats_reader, getEnvoyContainerStats())
      .WillOnce(Return(EnvoyContainerStats{true, 1000,5000000000, std::chrono::system_clock::time_point(std::chrono::milliseconds(1734711389000))}))
      .WillOnce(Return(EnvoyContainerStats{true, 1000,10000000000, std::chrono::system_clock::time_point(std::chrono::milliseconds(1734711394000))}))
      .WillOnce(Return(EnvoyContainerStats{true, 1000,15000000000, std::chrono::system_clock::time_point(std::chrono::milliseconds(1734711399000))}));
  auto monitor = std::make_unique<EnvoyContainerCpuUtilizationMonitor>(config, std::move(stats_reader));

  ResourcePressure resource;
  monitor->updateResourceUsage(resource);
  ASSERT_TRUE(resource.hasPressure());
  ASSERT_FALSE(resource.hasError());
  EXPECT_DOUBLE_EQ(resource.pressure(), 0.05); // dampening

  monitor->updateResourceUsage(resource);
  ASSERT_TRUE(resource.hasPressure());
  ASSERT_FALSE(resource.hasError());
  EXPECT_DOUBLE_EQ(resource.pressure(), 0.0975); // dampening
}

TEST(EnvoyContainerCpuUtilizationMonitorTest, GetsErroneousStatsTimeDiff) {
  envoy::extensions::resource_monitors::envoy_container_cpu_utilization::v3::EnvoyContainerCpuUtilizationConfig config;
  auto stats_reader = std::make_unique<MockContainerStatsReader>();
  EXPECT_CALL(*stats_reader, getEnvoyContainerStats())
      .WillOnce(Return(EnvoyContainerStats{true, 1000,5000000000, std::chrono::system_clock::time_point(std::chrono::milliseconds(1734711389000))}))
      .WillOnce(Return(EnvoyContainerStats{true, 1000,10000000000, std::chrono::system_clock::time_point(std::chrono::milliseconds(1734711389000))}));
  auto monitor = std::make_unique<EnvoyContainerCpuUtilizationMonitor>(config, std::move(stats_reader));

  ResourcePressure resource;
  monitor->updateResourceUsage(resource);
  ASSERT_TRUE(resource.hasError());
}

TEST(EnvoyContainerCpuUtilizationMonitorTest, GetsErroneousStatsCpuAllocated) {
  envoy::extensions::resource_monitors::envoy_container_cpu_utilization::v3::EnvoyContainerCpuUtilizationConfig config;
  auto stats_reader = std::make_unique<MockContainerStatsReader>();
  EXPECT_CALL(*stats_reader, getEnvoyContainerStats())
      .WillOnce(Return(EnvoyContainerStats{true, 1000,5000000000, std::chrono::system_clock::time_point(std::chrono::milliseconds(1734711389000))}))
      .WillOnce(Return(EnvoyContainerStats{true, 0,10000000000, std::chrono::system_clock::time_point(std::chrono::milliseconds(1734711394000))}));
  auto monitor = std::make_unique<EnvoyContainerCpuUtilizationMonitor>(config, std::move(stats_reader));

  ResourcePressure resource;
  monitor->updateResourceUsage(resource);
  ASSERT_TRUE(resource.hasError());
}


TEST(EnvoyContainerCpuUtilizationMonitorTest, GetsErroneousStatsCpuTimesDiff) {
  envoy::extensions::resource_monitors::envoy_container_cpu_utilization::v3::EnvoyContainerCpuUtilizationConfig config;
  auto stats_reader = std::make_unique<MockContainerStatsReader>();
  EXPECT_CALL(*stats_reader, getEnvoyContainerStats())
      .WillOnce(Return(EnvoyContainerStats{true, 1000,50000000000, std::chrono::system_clock::time_point(std::chrono::milliseconds(1734711389000))}))
      .WillOnce(Return(EnvoyContainerStats{true, 1000,10000000000, std::chrono::system_clock::time_point(std::chrono::milliseconds(1734711394000))}));
  auto monitor = std::make_unique<EnvoyContainerCpuUtilizationMonitor>(config, std::move(stats_reader));

  ResourcePressure resource;
  monitor->updateResourceUsage(resource);
  ASSERT_TRUE(resource.hasError());
}

TEST(EnvoyContainerCpuUtilizationMonitorTest, ReportsError) {
  envoy::extensions::resource_monitors::envoy_container_cpu_utilization::v3::EnvoyContainerCpuUtilizationConfig config;
  auto stats_reader = std::make_unique<MockContainerStatsReader>();
  EXPECT_CALL(*stats_reader, getEnvoyContainerStats())
      .WillOnce(Return(EnvoyContainerStats{false, 0,5000000000, std::chrono::system_clock::time_point(std::chrono::milliseconds(1734711389000))}))
      .WillOnce(Return(EnvoyContainerStats{false, 0,0, std::chrono::system_clock::time_point(std::chrono::milliseconds(1734711394000))}))
      .WillOnce(Return(EnvoyContainerStats{false,1000,0, std::chrono::system_clock::time_point(std::chrono::milliseconds(1734711399000))}));
  auto monitor = std::make_unique<EnvoyContainerCpuUtilizationMonitor>(config, std::move(stats_reader));

  ResourcePressure resource;
  monitor->updateResourceUsage(resource);
  ASSERT_TRUE(resource.hasError());

  monitor->updateResourceUsage(resource);
  ASSERT_TRUE(resource.hasError());
}

} // namespace
} // namespace EnvoyContainerCpuUtilizationMonitor
} // namespace ResourceMonitors
} // namespace Extensions
} // namespace Envoy
