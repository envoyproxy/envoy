#include "source/extensions/resource_monitors/cgroup_memory/cgroup_memory_stats_reader.h"

#include "test/test_common/environment.h"
#include "test/test_common/utility.h"

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

// Test implementation of V1 stats reader with configurable paths.
class TestCgroupV1StatsReader : public CgroupV1StatsReader {
public:
  TestCgroupV1StatsReader(const std::string& usage_path, const std::string& limit_path)
      : usage_path_(usage_path), limit_path_(limit_path) {}

  std::string getMemoryUsagePath() const override { return usage_path_; }
  std::string getMemoryLimitPath() const override { return limit_path_; }

private:
  const std::string usage_path_;
  const std::string limit_path_;
};

// Test implementation of V2 stats reader with configurable paths.
class TestCgroupV2StatsReader : public CgroupV2StatsReader {
public:
  TestCgroupV2StatsReader(const std::string& usage_path, const std::string& limit_path)
      : usage_path_(usage_path), limit_path_(limit_path) {}

  std::string getMemoryUsagePath() const override { return usage_path_; }
  std::string getMemoryLimitPath() const override { return limit_path_; }

private:
  const std::string usage_path_;
  const std::string limit_path_;
};

// Test class to expose protected methods for V1 reader
class TestableV1StatsReader : public CgroupV1StatsReader {
public:
  using CgroupV1StatsReader::getMemoryLimitPath;
  using CgroupV1StatsReader::getMemoryUsagePath;
};

// Test class to expose protected methods for V2 reader
class TestableV2StatsReader : public CgroupV2StatsReader {
public:
  using CgroupV2StatsReader::getMemoryLimitPath;
  using CgroupV2StatsReader::getMemoryUsagePath;
};

// Tests reading memory stats from cgroup v1 files.
TEST(CgroupMemoryStatsReaderTest, ReadsV1MemoryStats) {
  const std::string usage_path = TestEnvironment::temporaryPath("memory.usage_in_bytes");
  const std::string limit_path = TestEnvironment::temporaryPath("memory.limit_in_bytes");

  {
    AtomicFileUpdater file_updater_usage(usage_path);
    file_updater_usage.update("1000\n");
  }
  {
    AtomicFileUpdater file_updater_limit(limit_path);
    file_updater_limit.update("2000\n");
  }

  TestCgroupV1StatsReader stats_reader(usage_path, limit_path);
  EXPECT_EQ(stats_reader.getMemoryUsage(), 1000);
  EXPECT_EQ(stats_reader.getMemoryLimit(), 2000);
}

// Tests reading memory stats from cgroup v2 files.
TEST(CgroupMemoryStatsReaderTest, ReadsV2MemoryStats) {
  const std::string usage_path = TestEnvironment::temporaryPath("memory.current");
  const std::string limit_path = TestEnvironment::temporaryPath("memory.max");

  {
    AtomicFileUpdater file_updater_usage(usage_path);
    file_updater_usage.update("1500\n");
  }
  {
    AtomicFileUpdater file_updater_limit(limit_path);
    file_updater_limit.update("3000\n");
  }

  TestCgroupV2StatsReader stats_reader(usage_path, limit_path);
  EXPECT_EQ(stats_reader.getMemoryUsage(), 1500);
  EXPECT_EQ(stats_reader.getMemoryLimit(), 3000);
}

// Tests handling of "max" value in cgroup v2 limit file.
TEST(CgroupMemoryStatsReaderTest, HandlesV2MaxValue) {
  const std::string usage_path = TestEnvironment::temporaryPath("memory.current");
  const std::string limit_path = TestEnvironment::temporaryPath("memory.max");

  {
    AtomicFileUpdater file_updater_usage(usage_path);
    file_updater_usage.update("1000\n");
  }
  {
    AtomicFileUpdater file_updater_limit(limit_path);
    file_updater_limit.update("max\n");
  }

  TestCgroupV2StatsReader stats_reader(usage_path, limit_path);
  EXPECT_EQ(stats_reader.getMemoryUsage(), 1000);
  EXPECT_EQ(stats_reader.getMemoryLimit(), std::numeric_limits<uint64_t>::max());
}

// Tests handling of "-1" value in cgroup v1 limit file.
TEST(CgroupMemoryStatsReaderTest, HandlesV1UnlimitedValue) {
  const std::string limit_path = TestEnvironment::temporaryPath("memory.limit_in_bytes");
  {
    AtomicFileUpdater file_updater(limit_path);
    file_updater.update("-1\n"); // V1 format for unlimited
  }

  TestCgroupV1StatsReader stats_reader("dummy", limit_path);
  EXPECT_EQ(stats_reader.getMemoryLimit(), CgroupMemoryStatsReader::UNLIMITED_MEMORY);
}

// Tests that an exception is thrown when the memory stats file is missing.
TEST(CgroupMemoryStatsReaderTest, ThrowsOnMissingFile) {
  const std::string nonexistent_path = TestEnvironment::temporaryPath("nonexistent");
  TestCgroupV1StatsReader stats_reader(nonexistent_path, "dummy");
  EXPECT_THROW_WITH_MESSAGE(
      stats_reader.getMemoryUsage(), EnvoyException,
      fmt::format("Unable to open memory stats file at {}", nonexistent_path));
}

// Tests that an exception is thrown when the memory stats file contains invalid content.
TEST(CgroupMemoryStatsReaderTest, ThrowsOnInvalidContent) {
  const std::string invalid_path = TestEnvironment::temporaryPath("invalid");
  {
    AtomicFileUpdater file_updater(invalid_path);
    file_updater.update("not_a_number\n");
  }

  TestCgroupV1StatsReader stats_reader(invalid_path, "dummy");
  EXPECT_THROW(stats_reader.getMemoryUsage(), EnvoyException);
}

// Tests that an exception is thrown when the memory stats file is empty.
TEST(CgroupMemoryStatsReaderTest, ThrowsOnEmptyFile) {
  const std::string empty_path = TestEnvironment::temporaryPath("empty");
  {
    AtomicFileUpdater file_updater(empty_path);
    file_updater.update("");
  }

  TestCgroupV1StatsReader stats_reader(empty_path, "dummy");
  EXPECT_THROW_WITH_MESSAGE(stats_reader.getMemoryUsage(), EnvoyException,
                            fmt::format("Unable to read memory stats from file at {}", empty_path));
}

// Tests that an exception is thrown when the memory stats file is unreadable.
TEST(CgroupMemoryStatsReaderTest, ThrowsOnUnreadableFile) {
  const std::string unreadable_path = TestEnvironment::temporaryPath("unreadable");
  {
    AtomicFileUpdater file_updater(unreadable_path);
    file_updater.update("100\n");
    chmod(unreadable_path.c_str(), 0000);
  }

  TestCgroupV1StatsReader stats_reader(unreadable_path, "dummy");
  EXPECT_THROW_WITH_MESSAGE(stats_reader.getMemoryUsage(), EnvoyException,
                            fmt::format("Unable to open memory stats file at {}", unreadable_path));

  chmod(unreadable_path.c_str(), 0644);
}

// Tests that an exception is thrown when the memory stats file cannot be read.
TEST(CgroupMemoryStatsReaderTest, ThrowsOnReadError) {
  const std::string path = TestEnvironment::temporaryPath("read_error");
  {
    AtomicFileUpdater file_updater(path);
    file_updater.update("");
  }

  TestCgroupV1StatsReader stats_reader(path, "dummy");
  EXPECT_THROW_WITH_MESSAGE(stats_reader.getMemoryUsage(), EnvoyException,
                            fmt::format("Unable to read memory stats from file at {}", path));
}

TEST(CgroupMemoryStatsReaderTest, ThrowsOnWhitespaceOnlyFile) {
  const std::string whitespace_path = TestEnvironment::temporaryPath("whitespace");
  {
    AtomicFileUpdater file_updater(whitespace_path);
    file_updater.update("  \n\t  \n");
  }

  TestCgroupV1StatsReader stats_reader(whitespace_path, "dummy");
  EXPECT_THROW_WITH_MESSAGE(stats_reader.getMemoryUsage(), EnvoyException,
                            fmt::format("Empty memory stats file at {}", whitespace_path));
}

// Tests that factory creates V2 reader when V2 implementation is available.
TEST(CgroupMemoryStatsReaderTest, CreateReturnsV2ReaderWhenV2Available) {
  auto mock_file_system = std::make_unique<NiceMock<MockFileSystem>>();
  const auto* mock_ptr = mock_file_system.get();
  ON_CALL(*mock_file_system, exists).WillByDefault(Return(true));

  const FileSystem* original = &FileSystem::instance();
  FileSystem::setInstance(mock_ptr);

  EXPECT_CALL(*mock_file_system, exists(CgroupPaths::V2::getUsagePath())).WillOnce(Return(true));
  EXPECT_CALL(*mock_file_system, exists(CgroupPaths::V2::getLimitPath())).WillOnce(Return(true));

  auto reader = CgroupMemoryStatsReader::create();
  EXPECT_NE(dynamic_cast<CgroupV2StatsReader*>(reader.get()), nullptr);

  FileSystem::setInstance(original);
}

// Tests that factory falls back to V1 reader when V2 is not available.
TEST(CgroupMemoryStatsReaderTest, CreateReturnsV1ReaderWhenV1Available) {
  auto mock_file_system = std::make_unique<NiceMock<MockFileSystem>>();
  const auto* mock_ptr = mock_file_system.get();
  ON_CALL(*mock_file_system, exists).WillByDefault(Return(false));

  const FileSystem* original = &FileSystem::instance();
  FileSystem::setInstance(mock_ptr);

  EXPECT_CALL(*mock_file_system, exists(CgroupPaths::V2::getUsagePath())).WillOnce(Return(false));
  EXPECT_CALL(*mock_file_system, exists(CgroupPaths::CGROUP_V1_BASE)).WillOnce(Return(true));

  auto reader = CgroupMemoryStatsReader::create();
  EXPECT_NE(dynamic_cast<CgroupV1StatsReader*>(reader.get()), nullptr);

  FileSystem::setInstance(original);
}

// Tests that factory throws when no cgroup implementation is available.
TEST(CgroupMemoryStatsReaderTest, CreateThrowsWhenNoImplementationAvailable) {
  auto mock_file_system = std::make_unique<NiceMock<MockFileSystem>>();
  const auto* mock_ptr = mock_file_system.get();
  ON_CALL(*mock_file_system, exists).WillByDefault(Return(false));

  const FileSystem* original = &FileSystem::instance();
  FileSystem::setInstance(mock_ptr);

  EXPECT_CALL(*mock_file_system, exists(CgroupPaths::V2::getUsagePath())).WillOnce(Return(false));
  EXPECT_CALL(*mock_file_system, exists(CgroupPaths::CGROUP_V1_BASE)).WillOnce(Return(false));

  EXPECT_THROW_WITH_MESSAGE(CgroupMemoryStatsReader::create(), EnvoyException,
                            "No supported cgroup memory implementation found");

  FileSystem::setInstance(original);
}

// Tests that V1 reader returns correct paths
TEST(CgroupMemoryStatsReaderTest, V1ReaderReturnsPaths) {
  TestableV1StatsReader stats_reader;
  EXPECT_EQ(stats_reader.getMemoryUsagePath(), CgroupPaths::V1::getUsagePath());
  EXPECT_EQ(stats_reader.getMemoryLimitPath(), CgroupPaths::V1::getLimitPath());
}

// Tests that V2 reader returns correct paths
TEST(CgroupMemoryStatsReaderTest, V2ReaderReturnsPaths) {
  TestableV2StatsReader stats_reader;
  EXPECT_EQ(stats_reader.getMemoryUsagePath(), CgroupPaths::V2::getUsagePath());
  EXPECT_EQ(stats_reader.getMemoryLimitPath(), CgroupPaths::V2::getLimitPath());
}

} // namespace
} // namespace CgroupMemory
} // namespace ResourceMonitors
} // namespace Extensions
} // namespace Envoy
