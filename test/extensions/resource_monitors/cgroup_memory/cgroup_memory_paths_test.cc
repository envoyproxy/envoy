#include "source/extensions/resource_monitors/cgroup_memory/cgroup_memory_paths.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace ResourceMonitors {
namespace CgroupMemory {
namespace {

using testing::NiceMock;
using testing::Return;

// Mock filesystem implementation for testing.
class MockFilesystem : public Filesystem::Instance {
public:
  MOCK_METHOD(Filesystem::FilePtr, createFile, (const Filesystem::FilePathAndType&), (override));
  MOCK_METHOD(bool, fileExists, (const std::string&), (override));
  MOCK_METHOD(Api::IoCallResult<Filesystem::FileInfo>, stat, (absl::string_view), (override));
  MOCK_METHOD(Api::IoCallBoolResult, createPath, (absl::string_view), (override));
  MOCK_METHOD(bool, directoryExists, (const std::string&), (override));
  MOCK_METHOD(ssize_t, fileSize, (const std::string&), (override));
  MOCK_METHOD(absl::StatusOr<std::string>, fileReadToEnd, (const std::string&), (override));
  MOCK_METHOD(absl::StatusOr<Filesystem::PathSplitResult>, splitPathFromFilename,
              (absl::string_view), (override));
  MOCK_METHOD(bool, illegalPath, (const std::string&), (override));
};

// Tests that base paths and path construction methods work correctly.
TEST(CgroupMemoryPathsTest, TestCgroupBasePaths) {
  EXPECT_EQ(CgroupPaths::CGROUP_V1_BASE, "/sys/fs/cgroup/memory");
  EXPECT_EQ(CgroupPaths::CGROUP_V2_BASE, "/sys/fs/cgroup");

  EXPECT_EQ(CgroupPaths::V1::getUsagePath(),
            std::string(CgroupPaths::CGROUP_V1_BASE) + CgroupPaths::V1::USAGE);
  EXPECT_EQ(CgroupPaths::V1::getLimitPath(),
            std::string(CgroupPaths::CGROUP_V1_BASE) + CgroupPaths::V1::LIMIT);

  EXPECT_EQ(CgroupPaths::V2::getUsagePath(),
            std::string(CgroupPaths::CGROUP_V2_BASE) + CgroupPaths::V2::USAGE);
  EXPECT_EQ(CgroupPaths::V2::getLimitPath(),
            std::string(CgroupPaths::CGROUP_V2_BASE) + CgroupPaths::V2::LIMIT);
}

// Tests that cgroup v2 detection works when both required files exist.
TEST(CgroupMemoryPathsTest, DetectsCgroupV2) {
  NiceMock<MockFilesystem> mock_fs;
  ON_CALL(mock_fs, fileExists).WillByDefault(Return(true));

  EXPECT_CALL(mock_fs, fileExists(CgroupPaths::V2::getUsagePath())).WillOnce(Return(true));
  EXPECT_CALL(mock_fs, fileExists(CgroupPaths::V2::getLimitPath())).WillOnce(Return(true));

  EXPECT_TRUE(CgroupPaths::isV2(mock_fs));
}

// Tests that cgroup v1 detection works when the base directory exists.
TEST(CgroupMemoryPathsTest, DetectsCgroupV1) {
  NiceMock<MockFilesystem> mock_fs;
  ON_CALL(mock_fs, fileExists).WillByDefault(Return(true));

  EXPECT_CALL(mock_fs, fileExists(CgroupPaths::CGROUP_V1_BASE)).WillOnce(Return(true));

  EXPECT_TRUE(CgroupPaths::isV1(mock_fs));
}

// Tests that cgroup v2 detection fails when required files don't exist.
TEST(CgroupMemoryPathsTest, DetectsCgroupV2WhenFilesDoNotExist) {
  NiceMock<MockFilesystem> mock_fs;
  ON_CALL(mock_fs, fileExists).WillByDefault(Return(false));

  EXPECT_CALL(mock_fs, fileExists(CgroupPaths::V2::getUsagePath())).WillOnce(Return(false));
  EXPECT_CALL(mock_fs, fileExists(CgroupPaths::V2::getLimitPath())).Times(0);

  EXPECT_FALSE(CgroupPaths::isV2(mock_fs));
}

// Tests that cgroup v2 detection fails when only usage file exists.
TEST(CgroupMemoryPathsTest, DetectsCgroupV2WhenOnlyUsageExists) {
  NiceMock<MockFilesystem> mock_fs;

  EXPECT_CALL(mock_fs, fileExists(CgroupPaths::V2::getUsagePath())).WillOnce(Return(true));
  EXPECT_CALL(mock_fs, fileExists(CgroupPaths::V2::getLimitPath())).WillOnce(Return(false));

  EXPECT_FALSE(CgroupPaths::isV2(mock_fs));
}

// Tests that cgroup v1 detection fails when base directory doesn't exist.
TEST(CgroupMemoryPathsTest, DetectsCgroupV1WhenFileDoesNotExist) {
  NiceMock<MockFilesystem> mock_fs;
  ON_CALL(mock_fs, fileExists).WillByDefault(Return(false));

  EXPECT_CALL(mock_fs, fileExists(CgroupPaths::CGROUP_V1_BASE)).WillOnce(Return(false));

  EXPECT_FALSE(CgroupPaths::isV1(mock_fs));
}

} // namespace
} // namespace CgroupMemory
} // namespace ResourceMonitors
} // namespace Extensions
} // namespace Envoy
