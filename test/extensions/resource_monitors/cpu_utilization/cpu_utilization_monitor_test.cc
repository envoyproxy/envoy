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

TEST(CpuUtilizationMonitorTest, ComputesCorrectUsage) {
  envoy::extensions::resource_monitors::cpu_utilization::v3::CpuUtilizationConfig config;
  auto stats_reader = std::make_unique<MockCpuStatsReader>();
  EXPECT_CALL(*stats_reader, getCpuTimes())
      .WillOnce(Return(CpuTimes{true, 50, 100}))
      .WillOnce(Return(CpuTimes{true, 100, 200}))
      .WillOnce(Return(CpuTimes{true, 200, 300}));
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

TEST(CpuUtilizationMonitorTest, GetsErroneousStatsDenominator) {
  envoy::extensions::resource_monitors::cpu_utilization::v3::CpuUtilizationConfig config;
  auto stats_reader = std::make_unique<MockCpuStatsReader>();
  EXPECT_CALL(*stats_reader, getCpuTimes())
      .WillOnce(Return(CpuTimes{true, 100, 100}))
      .WillOnce(Return(CpuTimes{true, 100, 99}));
  auto monitor = std::make_unique<CpuUtilizationMonitor>(config, std::move(stats_reader));

  ResourcePressure resource;
  monitor->updateResourceUsage(resource);
  ASSERT_TRUE(resource.hasError());
}

TEST(CpuUtilizationMonitorTest, GetsErroneousStatsNumerator) {
  envoy::extensions::resource_monitors::cpu_utilization::v3::CpuUtilizationConfig config;
  auto stats_reader = std::make_unique<MockCpuStatsReader>();
  EXPECT_CALL(*stats_reader, getCpuTimes())
      .WillOnce(Return(CpuTimes{true, 100, 100}))
      .WillOnce(Return(CpuTimes{true, 99, 150}));
  auto monitor = std::make_unique<CpuUtilizationMonitor>(config, std::move(stats_reader));

  ResourcePressure resource;
  monitor->updateResourceUsage(resource);
  ASSERT_TRUE(resource.hasError());
}

TEST(CpuUtilizationMonitorTest, ReportsError) {
  envoy::extensions::resource_monitors::cpu_utilization::v3::CpuUtilizationConfig config;
  auto stats_reader = std::make_unique<MockCpuStatsReader>();
  EXPECT_CALL(*stats_reader, getCpuTimes())
      .WillOnce(Return(CpuTimes{false, 0, 0}))
      .WillOnce(Return(CpuTimes{false, 0, 0}))
      .WillOnce(Return(CpuTimes{false, 0, 200}));
  auto monitor = std::make_unique<CpuUtilizationMonitor>(config, std::move(stats_reader));

  ResourcePressure resource;
  monitor->updateResourceUsage(resource);
  ASSERT_TRUE(resource.hasError());

  monitor->updateResourceUsage(resource);
  ASSERT_TRUE(resource.hasError());
}

} // namespace
} // namespace CpuUtilizationMonitor
} // namespace ResourceMonitors
} // namespace Extensions
} // namespace Envoy
