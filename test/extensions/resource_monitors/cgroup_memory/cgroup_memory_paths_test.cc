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
class MockFileSystem : public FileSystem {
public:
  MOCK_METHOD(bool, exists, (const std::string&), (const, override));
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
  auto mock_file_system = std::make_unique<NiceMock<MockFileSystem>>();
  const auto* mock_ptr = mock_file_system.get();
  ON_CALL(*mock_file_system, exists).WillByDefault(Return(true));

  const FileSystem* original = &FileSystem::instance();
  FileSystem::setInstance(mock_ptr);

  EXPECT_CALL(*mock_file_system, exists(CgroupPaths::V2::getUsagePath())).WillOnce(Return(true));
  EXPECT_CALL(*mock_file_system, exists(CgroupPaths::V2::getLimitPath())).WillOnce(Return(true));

  EXPECT_TRUE(CgroupPaths::isV2());

  FileSystem::setInstance(original);
}

// Tests that cgroup v1 detection works when the base directory exists.
TEST(CgroupMemoryPathsTest, DetectsCgroupV1) {
  auto mock_file_system = std::make_unique<NiceMock<MockFileSystem>>();
  const auto* mock_ptr = mock_file_system.get();
  ON_CALL(*mock_file_system, exists).WillByDefault(Return(true));

  const FileSystem* original = &FileSystem::instance();
  FileSystem::setInstance(mock_ptr);

  EXPECT_CALL(*mock_file_system, exists(CgroupPaths::CGROUP_V1_BASE)).WillOnce(Return(true));

  EXPECT_TRUE(CgroupPaths::isV1());

  FileSystem::setInstance(original);
}

// Tests that cgroup v2 detection fails when required files don't exist.
TEST(CgroupMemoryPathsTest, DetectsCgroupV2WhenFilesDoNotExist) {
  auto mock_file_system = std::make_unique<NiceMock<MockFileSystem>>();
  const auto* mock_ptr = mock_file_system.get();
  ON_CALL(*mock_file_system, exists).WillByDefault(Return(false));

  const FileSystem* original = &FileSystem::instance();
  FileSystem::setInstance(mock_ptr);

  EXPECT_CALL(*mock_file_system, exists(CgroupPaths::V2::getUsagePath())).WillOnce(Return(false));
  EXPECT_CALL(*mock_file_system, exists(CgroupPaths::V2::getLimitPath())).Times(0);

  EXPECT_FALSE(CgroupPaths::isV2());

  FileSystem::setInstance(original);
}

// Tests that cgroup v2 detection fails when only usage file exists.
TEST(CgroupMemoryPathsTest, DetectsCgroupV2WhenOnlyUsageExists) {
  auto mock_file_system = std::make_unique<NiceMock<MockFileSystem>>();
  const auto* mock_ptr = mock_file_system.get();

  const FileSystem* original = &FileSystem::instance();
  FileSystem::setInstance(mock_ptr);

  EXPECT_CALL(*mock_file_system, exists(CgroupPaths::V2::getUsagePath())).WillOnce(Return(true));
  EXPECT_CALL(*mock_file_system, exists(CgroupPaths::V2::getLimitPath())).WillOnce(Return(false));

  EXPECT_FALSE(CgroupPaths::isV2());

  FileSystem::setInstance(original);
}

// Tests that cgroup v1 detection fails when base directory doesn't exist.
TEST(CgroupMemoryPathsTest, DetectsCgroupV1WhenFileDoesNotExist) {
  auto mock_file_system = std::make_unique<NiceMock<MockFileSystem>>();
  const auto* mock_ptr = mock_file_system.get();
  ON_CALL(*mock_file_system, exists).WillByDefault(Return(false));

  const FileSystem* original = &FileSystem::instance();
  FileSystem::setInstance(mock_ptr);

  EXPECT_CALL(*mock_file_system, exists(CgroupPaths::CGROUP_V1_BASE)).WillOnce(Return(false));

  EXPECT_FALSE(CgroupPaths::isV1());

  FileSystem::setInstance(original);
}

// Tests that cleanup properly restores the original filesystem instance.
TEST(CgroupMemoryPathsTest, CleanupRestoresOriginalInstance) {
  const FileSystem* original = &FileSystem::instance();

  {
    auto scoped_mock = std::make_unique<NiceMock<MockFileSystem>>();
    FileSystem::setInstance(scoped_mock.get());

    EXPECT_CALL(*scoped_mock, exists("/some/path")).WillOnce(Return(true));
    EXPECT_TRUE(FileSystem::instance().exists("/some/path"));
  }

  FileSystem::setInstance(original);

  EXPECT_NO_THROW(FileSystem::instance().exists("/some/path"));
}

} // namespace
} // namespace CgroupMemory
} // namespace ResourceMonitors
} // namespace Extensions
} // namespace Envoy
