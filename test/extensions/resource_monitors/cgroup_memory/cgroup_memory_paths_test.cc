#include "source/extensions/resource_monitors/cgroup_memory/cgroup_memory_paths.h"

#include "test/mocks/filesystem/mocks.h"

#include "gtest/gtest.h"

using testing::Return;

namespace Envoy {
namespace Extensions {
namespace ResourceMonitors {
namespace CgroupMemory {
namespace {

TEST(CgroupPathsTest, V1Paths) {
  // Test that the paths are correctly constructed
  EXPECT_EQ(CgroupPaths::V1::getBasePath(), "/sys/fs/cgroup/memory");
  EXPECT_EQ(CgroupPaths::V1::getUsagePath(), "/sys/fs/cgroup/memory/memory.usage_in_bytes");
  EXPECT_EQ(CgroupPaths::V1::getLimitPath(), "/sys/fs/cgroup/memory/memory.limit_in_bytes");
}

TEST(CgroupPathsTest, V2Paths) {
  // Test that the paths are correctly constructed
  EXPECT_EQ(CgroupPaths::V2::getUsagePath(), "/sys/fs/cgroup/memory.current");
  EXPECT_EQ(CgroupPaths::V2::getLimitPath(), "/sys/fs/cgroup/memory.max");
}

TEST(CgroupPathsTest, IsV1) {
  // Mock filesystem
  testing::NiceMock<Filesystem::MockInstance> mock_fs;

  // Test when cgroup v1 is available
  EXPECT_CALL(mock_fs, fileExists(CgroupPaths::V1::getBasePath())).WillOnce(Return(true));
  EXPECT_TRUE(CgroupPaths::isV1(mock_fs));

  // Test when cgroup v1 is not available
  EXPECT_CALL(mock_fs, fileExists(CgroupPaths::V1::getBasePath())).WillOnce(Return(false));
  EXPECT_FALSE(CgroupPaths::isV1(mock_fs));
}

TEST(CgroupPathsTest, IsV2) {
  // Mock filesystem
  testing::NiceMock<Filesystem::MockInstance> mock_fs;

  // Test when cgroup v2 is available
  EXPECT_CALL(mock_fs, fileExists(CgroupPaths::V2::getUsagePath())).WillOnce(Return(true));
  EXPECT_CALL(mock_fs, fileExists(CgroupPaths::V2::getLimitPath())).WillOnce(Return(true));
  EXPECT_TRUE(CgroupPaths::isV2(mock_fs));

  // Test when cgroup v2 is not available (usage file missing)
  EXPECT_CALL(mock_fs, fileExists(CgroupPaths::V2::getUsagePath())).WillOnce(Return(false));
  EXPECT_FALSE(CgroupPaths::isV2(mock_fs));

  // Test when cgroup v2 is not available (limit file missing)
  EXPECT_CALL(mock_fs, fileExists(CgroupPaths::V2::getUsagePath())).WillOnce(Return(true));
  EXPECT_CALL(mock_fs, fileExists(CgroupPaths::V2::getLimitPath())).WillOnce(Return(false));
  EXPECT_FALSE(CgroupPaths::isV2(mock_fs));
}

} // namespace
} // namespace CgroupMemory
} // namespace ResourceMonitors
} // namespace Extensions
} // namespace Envoy
