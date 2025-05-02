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
  config.set_max_memory_bytes(250);

  testing::NiceMock<Filesystem::MockInstance> mock_fs;

  // Mock the filesystem to indicate that cgroup v2 paths exist
  ON_CALL(mock_fs, fileExists).WillByDefault(Return(false));
  EXPECT_CALL(mock_fs, fileExists(CgroupPaths::V2::getUsagePath())).WillRepeatedly(Return(true));
  EXPECT_CALL(mock_fs, fileExists(CgroupPaths::V2::getLimitPath())).WillRepeatedly(Return(true));

  // Mock the file reads to return memory usage and limit
  EXPECT_CALL(mock_fs, fileReadToEnd(CgroupPaths::V2::getUsagePath()))
      .WillOnce(Return(absl::StatusOr<std::string>("500")));
  EXPECT_CALL(mock_fs, fileReadToEnd(CgroupPaths::V2::getLimitPath()))
      .WillOnce(Return(absl::StatusOr<std::string>("500")));

  auto monitor = std::make_unique<CgroupMemoryMonitor>(config, mock_fs);

  ResourcePressure resource;
  monitor->updateResourceUsage(resource);
  ASSERT_TRUE(resource.hasPressure());
  ASSERT_FALSE(resource.hasError());
  EXPECT_DOUBLE_EQ(resource.pressure(), 2.0);
}

// Test that the monitor computes correct usage using the cgroup limit
TEST(CgroupMemoryMonitorTest, ComputesCorrectUsageUsingCgroupLimit) {
  envoy::extensions::resource_monitors::cgroup_memory::v3::CgroupMemoryConfig config;

  testing::NiceMock<Filesystem::MockInstance> mock_fs;

  // Mock the filesystem to indicate that cgroup v2 paths exist
  ON_CALL(mock_fs, fileExists).WillByDefault(Return(false));
  EXPECT_CALL(mock_fs, fileExists(CgroupPaths::V2::getUsagePath())).WillRepeatedly(Return(true));
  EXPECT_CALL(mock_fs, fileExists(CgroupPaths::V2::getLimitPath())).WillRepeatedly(Return(true));

  // Mock the file reads to return memory usage and limit
  EXPECT_CALL(mock_fs, fileReadToEnd(CgroupPaths::V2::getUsagePath()))
      .WillOnce(Return(absl::StatusOr<std::string>("500")));
  EXPECT_CALL(mock_fs, fileReadToEnd(CgroupPaths::V2::getLimitPath()))
      .WillOnce(Return(absl::StatusOr<std::string>("2000")));

  auto monitor = std::make_unique<CgroupMemoryMonitor>(config, mock_fs);

  ResourcePressure resource;
  monitor->updateResourceUsage(resource);
  ASSERT_TRUE(resource.hasPressure());
  ASSERT_FALSE(resource.hasError());
  EXPECT_DOUBLE_EQ(resource.pressure(), 0.25);
}

// Test that the monitor reports correct pressure when usage exceeds the limit
TEST(CgroupMemoryMonitorTest, UsageExceedsLimit) {
  envoy::extensions::resource_monitors::cgroup_memory::v3::CgroupMemoryConfig config;

  testing::NiceMock<Filesystem::MockInstance> mock_fs;

  // Mock the filesystem to indicate that cgroup v2 paths exist
  ON_CALL(mock_fs, fileExists).WillByDefault(Return(false));
  EXPECT_CALL(mock_fs, fileExists(CgroupPaths::V2::getUsagePath())).WillRepeatedly(Return(true));
  EXPECT_CALL(mock_fs, fileExists(CgroupPaths::V2::getLimitPath())).WillRepeatedly(Return(true));

  // Mock the file reads to return memory usage and limit
  EXPECT_CALL(mock_fs, fileReadToEnd(CgroupPaths::V2::getUsagePath()))
      .WillOnce(Return(absl::StatusOr<std::string>("2000"))); // Usage higher than limit
  EXPECT_CALL(mock_fs, fileReadToEnd(CgroupPaths::V2::getLimitPath()))
      .WillOnce(Return(absl::StatusOr<std::string>("1000")));

  auto monitor = std::make_unique<CgroupMemoryMonitor>(config, mock_fs);

  ResourcePressure resource;
  monitor->updateResourceUsage(resource);
  ASSERT_TRUE(resource.hasPressure());
  ASSERT_FALSE(resource.hasError());
  // Should report pressure > 1.0 indicating over-utilization
  EXPECT_DOUBLE_EQ(resource.pressure(), 2.0);
}

// Test that the monitor handles various unlimited memory scenarios
TEST(CgroupMemoryMonitorTest, HandlesUnlimitedMemoryScenarios) {
  // Test case 1: Unlimited cgroup memory
  {
    envoy::extensions::resource_monitors::cgroup_memory::v3::CgroupMemoryConfig config;
    testing::NiceMock<Filesystem::MockInstance> mock_fs;

    // Mock the filesystem to indicate that cgroup v2 paths exist
    ON_CALL(mock_fs, fileExists).WillByDefault(Return(false));
    EXPECT_CALL(mock_fs, fileExists(CgroupPaths::V2::getUsagePath())).WillRepeatedly(Return(true));
    EXPECT_CALL(mock_fs, fileExists(CgroupPaths::V2::getLimitPath())).WillRepeatedly(Return(true));

    // Mock the file reads to return memory usage and unlimited limit
    EXPECT_CALL(mock_fs, fileReadToEnd(CgroupPaths::V2::getUsagePath()))
        .WillOnce(Return(absl::StatusOr<std::string>("500")));
    EXPECT_CALL(mock_fs, fileReadToEnd(CgroupPaths::V2::getLimitPath()))
        .WillOnce(Return(absl::StatusOr<std::string>("max"))); // "max" means unlimited in cgroup v2

    auto monitor = std::make_unique<CgroupMemoryMonitor>(config, mock_fs);

    ResourcePressure resource;
    monitor->updateResourceUsage(resource);
    ASSERT_TRUE(resource.hasPressure());
    ASSERT_FALSE(resource.hasError());
    // Unlimited cgroup memory means no pressure
    EXPECT_DOUBLE_EQ(resource.pressure(), 0.0);
  }

  // Test case 2: Unlimited configured memory
  {
    envoy::extensions::resource_monitors::cgroup_memory::v3::CgroupMemoryConfig config;
    config.set_max_memory_bytes(CgroupMemoryStatsReader::UNLIMITED_MEMORY);
    testing::NiceMock<Filesystem::MockInstance> mock_fs;

    // Mock the filesystem to indicate that cgroup v2 paths exist
    ON_CALL(mock_fs, fileExists).WillByDefault(Return(false));
    EXPECT_CALL(mock_fs, fileExists(CgroupPaths::V2::getUsagePath())).WillRepeatedly(Return(true));
    EXPECT_CALL(mock_fs, fileExists(CgroupPaths::V2::getLimitPath())).WillRepeatedly(Return(true));

    // Mock the file reads to return memory usage and limit
    EXPECT_CALL(mock_fs, fileReadToEnd(CgroupPaths::V2::getUsagePath()))
        .WillOnce(Return(absl::StatusOr<std::string>("500")));
    EXPECT_CALL(mock_fs, fileReadToEnd(CgroupPaths::V2::getLimitPath()))
        .WillOnce(Return(absl::StatusOr<std::string>("500")));

    auto monitor = std::make_unique<CgroupMemoryMonitor>(config, mock_fs);

    ResourcePressure resource;
    monitor->updateResourceUsage(resource);
    EXPECT_TRUE(resource.hasPressure());
    EXPECT_FALSE(resource.hasError());
    EXPECT_DOUBLE_EQ(resource.pressure(),
                     1.0); // Should report no pressure when cgroup limit is unlimited
  }

  // Test case 3: Both limits unlimited
  {
    envoy::extensions::resource_monitors::cgroup_memory::v3::CgroupMemoryConfig config;
    config.set_max_memory_bytes(CgroupMemoryStatsReader::UNLIMITED_MEMORY);
    testing::NiceMock<Filesystem::MockInstance> mock_fs;

    // Mock the filesystem to indicate that cgroup v2 paths exist
    ON_CALL(mock_fs, fileExists).WillByDefault(Return(false));
    EXPECT_CALL(mock_fs, fileExists(CgroupPaths::V2::getUsagePath())).WillRepeatedly(Return(true));
    EXPECT_CALL(mock_fs, fileExists(CgroupPaths::V2::getLimitPath())).WillRepeatedly(Return(true));

    // Mock the file reads to return memory usage and unlimited limit
    EXPECT_CALL(mock_fs, fileReadToEnd(CgroupPaths::V2::getUsagePath()))
        .WillOnce(Return(absl::StatusOr<std::string>("500")));
    EXPECT_CALL(mock_fs, fileReadToEnd(CgroupPaths::V2::getLimitPath()))
        .WillOnce(Return(absl::StatusOr<std::string>("max"))); // "max" means unlimited in cgroup v2

    auto monitor = std::make_unique<CgroupMemoryMonitor>(config, mock_fs);

    ResourcePressure resource;
    monitor->updateResourceUsage(resource);
    EXPECT_TRUE(resource.hasPressure());
    EXPECT_FALSE(resource.hasError());
    EXPECT_DOUBLE_EQ(resource.pressure(),
                     0.0); // Should report no pressure when both limits are unlimited
  }
}

// Test that the monitor handles various error scenarios
TEST(CgroupMemoryMonitorTest, HandlesErrorScenarios) {
  // Test case 1: Error from stats reader
  {
    envoy::extensions::resource_monitors::cgroup_memory::v3::CgroupMemoryConfig config;
    testing::NiceMock<Filesystem::MockInstance> mock_fs;

    // Mock the filesystem to indicate that cgroup v2 paths exist
    ON_CALL(mock_fs, fileExists).WillByDefault(Return(false));
    EXPECT_CALL(mock_fs, fileExists(CgroupPaths::V2::getUsagePath())).WillRepeatedly(Return(true));
    EXPECT_CALL(mock_fs, fileExists(CgroupPaths::V2::getLimitPath())).WillRepeatedly(Return(true));

    // Create monitor first
    auto monitor = std::make_unique<CgroupMemoryMonitor>(config, mock_fs);

    // Then mock the file read to fail
    EXPECT_CALL(mock_fs, fileReadToEnd(CgroupPaths::V2::getUsagePath()))
        .WillOnce(Return(absl::Status(absl::StatusCode::kNotFound, "File not found")));

    ResourcePressure resource;
    monitor->updateResourceUsage(resource);
    EXPECT_FALSE(resource.hasPressure());
    EXPECT_TRUE(resource.hasError());
    EXPECT_THAT(resource.error().what(), testing::HasSubstr("Unable to read memory stats file"));
  }

  // Test case 2: No implementation available
  {
    envoy::extensions::resource_monitors::cgroup_memory::v3::CgroupMemoryConfig config;
    testing::NiceMock<Filesystem::MockInstance> mock_fs;

    // Mock the filesystem to indicate that neither cgroup v1 nor v2 paths exist
    ON_CALL(mock_fs, fileExists).WillByDefault(Return(false));

    // Set up expectations before creating the monitor
    EXPECT_CALL(mock_fs, fileExists(CgroupPaths::V2::getUsagePath())).WillRepeatedly(Return(false));
    EXPECT_CALL(mock_fs, fileExists(CgroupPaths::V2::getLimitPath())).WillRepeatedly(Return(false));
    EXPECT_CALL(mock_fs, fileExists(CgroupPaths::V1::getBasePath())).WillRepeatedly(Return(false));

    EXPECT_THROW(
        { auto monitor = std::make_unique<CgroupMemoryMonitor>(config, mock_fs); }, EnvoyException);
  }
}

} // namespace
} // namespace CgroupMemory
} // namespace ResourceMonitors
} // namespace Extensions
} // namespace Envoy
