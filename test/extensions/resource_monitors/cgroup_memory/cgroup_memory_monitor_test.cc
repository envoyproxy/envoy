#include "source/extensions/resource_monitors/cgroup_memory/cgroup_memory_monitor.h"
#include "source/extensions/resource_monitors/cgroup_memory/cgroup_memory_paths.h"
#include "source/extensions/resource_monitors/cgroup_memory/cgroup_memory_stats_reader.h"

#include "test/mocks/filesystem/mocks.h"
#include "test/test_common/environment.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::NiceMock;
using testing::Return;

namespace Envoy {
namespace Extensions {
namespace ResourceMonitors {
namespace CgroupMemory {
namespace {

class ResourcePressure : public Server::ResourceUpdateCallbacks {
public:
  void onSuccess(const Server::ResourceUsage& usage) override {
    pressure_ = usage.resource_pressure_;
  }

  void onFailure(const absl::Status& error) override { error_ = error; }

  bool hasPressure() const { return pressure_.has_value(); }
  bool hasError() const { return error_.has_value(); }

  double pressure() const { return *pressure_; }
  const absl::Status& error() const { return *error_; }

private:
  std::optional<double> pressure_;
  std::optional<absl::Status> error_;
};

// Helper to create a V2 stats reader backed by a mock filesystem.
CgroupMemoryMonitor::StatsReaderPtr createV2Reader(Filesystem::MockInstance& mock_fs) {
  return std::make_unique<CgroupV2StatsReader>(mock_fs);
}

TEST(CgroupMemoryMonitorTest, ComputesCorrectUsageUsingConfigLimit) {
  envoy::extensions::resource_monitors::cgroup_memory::v3::CgroupMemoryConfig config;
  config.set_max_memory_bytes(250);

  testing::NiceMock<Filesystem::MockInstance> mock_fs;

  EXPECT_CALL(mock_fs, fileReadToEnd(CgroupPaths::V2::getUsagePath()))
      .WillOnce(Return(absl::StatusOr<std::string>("500")));
  EXPECT_CALL(mock_fs, fileReadToEnd(CgroupPaths::V2::getLimitPath()))
      .WillOnce(Return(absl::StatusOr<std::string>("500")));

  auto monitor = std::make_unique<CgroupMemoryMonitor>(config, createV2Reader(mock_fs));

  ResourcePressure resource;
  monitor->updateResourceUsage(resource);
  ASSERT_TRUE(resource.hasPressure());
  ASSERT_FALSE(resource.hasError());
  EXPECT_DOUBLE_EQ(resource.pressure(), 2.0);
}

TEST(CgroupMemoryMonitorTest, ComputesCorrectUsageUsingCgroupLimit) {
  envoy::extensions::resource_monitors::cgroup_memory::v3::CgroupMemoryConfig config;

  testing::NiceMock<Filesystem::MockInstance> mock_fs;

  EXPECT_CALL(mock_fs, fileReadToEnd(CgroupPaths::V2::getUsagePath()))
      .WillOnce(Return(absl::StatusOr<std::string>("500")));
  EXPECT_CALL(mock_fs, fileReadToEnd(CgroupPaths::V2::getLimitPath()))
      .WillOnce(Return(absl::StatusOr<std::string>("2000")));

  auto monitor = std::make_unique<CgroupMemoryMonitor>(config, createV2Reader(mock_fs));

  ResourcePressure resource;
  monitor->updateResourceUsage(resource);
  ASSERT_TRUE(resource.hasPressure());
  ASSERT_FALSE(resource.hasError());
  EXPECT_DOUBLE_EQ(resource.pressure(), 0.25);
}

TEST(CgroupMemoryMonitorTest, UsageExceedsLimit) {
  envoy::extensions::resource_monitors::cgroup_memory::v3::CgroupMemoryConfig config;

  testing::NiceMock<Filesystem::MockInstance> mock_fs;

  EXPECT_CALL(mock_fs, fileReadToEnd(CgroupPaths::V2::getUsagePath()))
      .WillOnce(Return(absl::StatusOr<std::string>("2000")));
  EXPECT_CALL(mock_fs, fileReadToEnd(CgroupPaths::V2::getLimitPath()))
      .WillOnce(Return(absl::StatusOr<std::string>("1000")));

  auto monitor = std::make_unique<CgroupMemoryMonitor>(config, createV2Reader(mock_fs));

  ResourcePressure resource;
  monitor->updateResourceUsage(resource);
  ASSERT_TRUE(resource.hasPressure());
  ASSERT_FALSE(resource.hasError());
  EXPECT_DOUBLE_EQ(resource.pressure(), 2.0);
}

TEST(CgroupMemoryMonitorTest, HandlesUnlimitedMemoryScenarios) {
  // Test case 1: Unlimited cgroup memory
  {
    envoy::extensions::resource_monitors::cgroup_memory::v3::CgroupMemoryConfig config;
    testing::NiceMock<Filesystem::MockInstance> mock_fs;

    EXPECT_CALL(mock_fs, fileReadToEnd(CgroupPaths::V2::getUsagePath()))
        .WillOnce(Return(absl::StatusOr<std::string>("500")));
    EXPECT_CALL(mock_fs, fileReadToEnd(CgroupPaths::V2::getLimitPath()))
        .WillOnce(Return(absl::StatusOr<std::string>("max")));

    auto monitor = std::make_unique<CgroupMemoryMonitor>(config, createV2Reader(mock_fs));

    ResourcePressure resource;
    monitor->updateResourceUsage(resource);
    ASSERT_TRUE(resource.hasPressure());
    ASSERT_FALSE(resource.hasError());
    EXPECT_DOUBLE_EQ(resource.pressure(), 0.0);
  }

  // Test case 2: Unlimited configured memory
  {
    envoy::extensions::resource_monitors::cgroup_memory::v3::CgroupMemoryConfig config;
    config.set_max_memory_bytes(CgroupMemoryStatsReader::UNLIMITED_MEMORY);
    testing::NiceMock<Filesystem::MockInstance> mock_fs;

    EXPECT_CALL(mock_fs, fileReadToEnd(CgroupPaths::V2::getUsagePath()))
        .WillOnce(Return(absl::StatusOr<std::string>("500")));
    EXPECT_CALL(mock_fs, fileReadToEnd(CgroupPaths::V2::getLimitPath()))
        .WillOnce(Return(absl::StatusOr<std::string>("500")));

    auto monitor = std::make_unique<CgroupMemoryMonitor>(config, createV2Reader(mock_fs));

    ResourcePressure resource;
    monitor->updateResourceUsage(resource);
    EXPECT_TRUE(resource.hasPressure());
    EXPECT_FALSE(resource.hasError());
    EXPECT_DOUBLE_EQ(resource.pressure(), 1.0);
  }

  // Test case 3: Both limits unlimited
  {
    envoy::extensions::resource_monitors::cgroup_memory::v3::CgroupMemoryConfig config;
    config.set_max_memory_bytes(CgroupMemoryStatsReader::UNLIMITED_MEMORY);
    testing::NiceMock<Filesystem::MockInstance> mock_fs;

    EXPECT_CALL(mock_fs, fileReadToEnd(CgroupPaths::V2::getUsagePath()))
        .WillOnce(Return(absl::StatusOr<std::string>("500")));
    EXPECT_CALL(mock_fs, fileReadToEnd(CgroupPaths::V2::getLimitPath()))
        .WillOnce(Return(absl::StatusOr<std::string>("max")));

    auto monitor = std::make_unique<CgroupMemoryMonitor>(config, createV2Reader(mock_fs));

    ResourcePressure resource;
    monitor->updateResourceUsage(resource);
    EXPECT_TRUE(resource.hasPressure());
    EXPECT_FALSE(resource.hasError());
    EXPECT_DOUBLE_EQ(resource.pressure(), 0.0);
  }
}

TEST(CgroupMemoryMonitorTest, HandlesUsageReadError) {
  envoy::extensions::resource_monitors::cgroup_memory::v3::CgroupMemoryConfig config;
  testing::NiceMock<Filesystem::MockInstance> mock_fs;

  auto monitor = std::make_unique<CgroupMemoryMonitor>(config, createV2Reader(mock_fs));

  EXPECT_CALL(mock_fs, fileReadToEnd(CgroupPaths::V2::getUsagePath()))
      .WillOnce(Return(absl::Status(absl::StatusCode::kNotFound, "File not found")));

  ResourcePressure resource;
  monitor->updateResourceUsage(resource);
  EXPECT_FALSE(resource.hasPressure());
  EXPECT_TRUE(resource.hasError());
  EXPECT_THAT(resource.error().message(), testing::HasSubstr("Unable to read memory stats file"));
}

TEST(CgroupMemoryMonitorTest, HandlesLimitReadError) {
  envoy::extensions::resource_monitors::cgroup_memory::v3::CgroupMemoryConfig config;
  testing::NiceMock<Filesystem::MockInstance> mock_fs;

  auto monitor = std::make_unique<CgroupMemoryMonitor>(config, createV2Reader(mock_fs));

  EXPECT_CALL(mock_fs, fileReadToEnd(CgroupPaths::V2::getUsagePath()))
      .WillOnce(Return(absl::StatusOr<std::string>("500")));
  EXPECT_CALL(mock_fs, fileReadToEnd(CgroupPaths::V2::getLimitPath()))
      .WillOnce(Return(absl::Status(absl::StatusCode::kNotFound, "File not found")));

  ResourcePressure resource;
  monitor->updateResourceUsage(resource);
  EXPECT_FALSE(resource.hasPressure());
  EXPECT_TRUE(resource.hasError());
  EXPECT_THAT(resource.error().message(), testing::HasSubstr("Unable to read memory stats file"));
}

} // namespace
} // namespace CgroupMemory
} // namespace ResourceMonitors
} // namespace Extensions
} // namespace Envoy
