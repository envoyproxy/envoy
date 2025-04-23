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
using testing::Throw;

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

// Test implementation of V1 stats reader with configurable paths.
class TestCgroupV1StatsReader : public CgroupV1StatsReader {
public:
  TestCgroupV1StatsReader(Filesystem::Instance& fs, const std::string& usage_path,
                          const std::string& limit_path)
      : CgroupV1StatsReader(fs), usage_path_(usage_path), limit_path_(limit_path) {}

  std::string getMemoryUsagePath() const override { return usage_path_; }
  std::string getMemoryLimitPath() const override { return limit_path_; }

private:
  const std::string usage_path_;
  const std::string limit_path_;
};

// Test implementation of V2 stats reader with configurable paths.
class TestCgroupV2StatsReader : public CgroupV2StatsReader {
public:
  TestCgroupV2StatsReader(Filesystem::Instance& fs, const std::string& usage_path,
                          const std::string& limit_path)
      : CgroupV2StatsReader(fs), usage_path_(usage_path), limit_path_(limit_path) {}

  std::string getMemoryUsagePath() const override { return usage_path_; }
  std::string getMemoryLimitPath() const override { return limit_path_; }

private:
  const std::string usage_path_;
  const std::string limit_path_;
};

// Test class to expose protected methods for V1 reader
class TestableV1StatsReader : public CgroupV1StatsReader {
public:
  explicit TestableV1StatsReader(Filesystem::Instance& fs) : CgroupV1StatsReader(fs) {}
  using CgroupV1StatsReader::getMemoryLimitPath;
  using CgroupV1StatsReader::getMemoryUsagePath;
};

// Test class to expose protected methods for V2 reader
class TestableV2StatsReader : public CgroupV2StatsReader {
public:
  explicit TestableV2StatsReader(Filesystem::Instance& fs) : CgroupV2StatsReader(fs) {}
  using CgroupV2StatsReader::getMemoryLimitPath;
  using CgroupV2StatsReader::getMemoryUsagePath;
};

// Tests that the stats reader correctly reads memory usage from a file.
TEST(CgroupMemoryStatsReaderTest, ReadsMemoryUsage) {
  NiceMock<MockFilesystem> mock_fs;

  // Mock fileExists to return true for V1 paths
  ON_CALL(mock_fs, fileExists(CgroupPaths::V2::getUsagePath())).WillByDefault(Return(false));
  ON_CALL(mock_fs, fileExists(CgroupPaths::V2::getLimitPath())).WillByDefault(Return(false));
  ON_CALL(mock_fs, fileExists(CgroupPaths::CGROUP_V1_BASE)).WillByDefault(Return(true));

  EXPECT_CALL(mock_fs, fileReadToEnd(CgroupPaths::V1::getUsagePath()))
      .WillOnce(Return(absl::StatusOr<std::string>("500")));

  auto reader = CgroupV1StatsReader::create(mock_fs);
  ASSERT_NE(reader, nullptr);
  EXPECT_EQ(reader->getMemoryUsage(), 500);
}

// Tests that the stats reader correctly reads memory limit from a file.
TEST(CgroupMemoryStatsReaderTest, ReadsMemoryLimit) {
  NiceMock<MockFilesystem> mock_fs;

  // Mock fileExists to return true for V1 paths
  ON_CALL(mock_fs, fileExists(CgroupPaths::V2::getUsagePath())).WillByDefault(Return(false));
  ON_CALL(mock_fs, fileExists(CgroupPaths::V2::getLimitPath())).WillByDefault(Return(false));
  ON_CALL(mock_fs, fileExists(CgroupPaths::CGROUP_V1_BASE)).WillByDefault(Return(true));

  EXPECT_CALL(mock_fs, fileReadToEnd(CgroupPaths::V1::getLimitPath()))
      .WillOnce(Return(absl::StatusOr<std::string>("1000")));

  auto reader = CgroupV1StatsReader::create(mock_fs);
  ASSERT_NE(reader, nullptr);
  EXPECT_EQ(reader->getMemoryLimit(), 1000);
}

// Tests that the stats reader handles empty files.
TEST(CgroupMemoryStatsReaderTest, HandlesEmptyFiles) {
  NiceMock<MockFilesystem> mock_fs;

  // Mock fileExists to return true for V1 paths
  ON_CALL(mock_fs, fileExists(CgroupPaths::V2::getUsagePath())).WillByDefault(Return(false));
  ON_CALL(mock_fs, fileExists(CgroupPaths::V2::getLimitPath())).WillByDefault(Return(false));
  ON_CALL(mock_fs, fileExists(CgroupPaths::CGROUP_V1_BASE)).WillByDefault(Return(true));

  EXPECT_CALL(mock_fs, fileReadToEnd(CgroupPaths::V1::getUsagePath()))
      .WillOnce(Return(absl::StatusOr<std::string>("")));

  auto reader = CgroupV1StatsReader::create(mock_fs);
  ASSERT_NE(reader, nullptr);
  EXPECT_THROW(reader->getMemoryUsage(), EnvoyException);
}

// Tests that the stats reader handles invalid values.
TEST(CgroupMemoryStatsReaderTest, HandlesInvalidValues) {
  NiceMock<MockFilesystem> mock_fs;

  // Mock fileExists to return true for V1 paths
  ON_CALL(mock_fs, fileExists(CgroupPaths::V2::getUsagePath())).WillByDefault(Return(false));
  ON_CALL(mock_fs, fileExists(CgroupPaths::V2::getLimitPath())).WillByDefault(Return(false));
  ON_CALL(mock_fs, fileExists(CgroupPaths::CGROUP_V1_BASE)).WillByDefault(Return(true));

  EXPECT_CALL(mock_fs, fileReadToEnd(CgroupPaths::V1::getUsagePath()))
      .WillOnce(Return(absl::StatusOr<std::string>("invalid")));

  auto reader = CgroupV1StatsReader::create(mock_fs);
  ASSERT_NE(reader, nullptr);
  EXPECT_THROW(reader->getMemoryUsage(), EnvoyException);
}

// Tests that the stats reader handles file read errors.
TEST(CgroupMemoryStatsReaderTest, HandlesFileReadErrors) {
  NiceMock<MockFilesystem> mock_fs;

  // Mock fileExists to return true for V1 paths
  ON_CALL(mock_fs, fileExists(CgroupPaths::V2::getUsagePath())).WillByDefault(Return(false));
  ON_CALL(mock_fs, fileExists(CgroupPaths::V2::getLimitPath())).WillByDefault(Return(false));
  ON_CALL(mock_fs, fileExists(CgroupPaths::CGROUP_V1_BASE)).WillByDefault(Return(true));

  EXPECT_CALL(mock_fs, fileReadToEnd(CgroupPaths::V1::getUsagePath()))
      .WillOnce(Return(absl::InvalidArgumentError("Failed to read file")));

  auto reader = CgroupV1StatsReader::create(mock_fs);
  ASSERT_NE(reader, nullptr);
  EXPECT_THROW(reader->getMemoryUsage(), EnvoyException);
}

// Tests that the stats reader handles unlimited memory.
TEST(CgroupMemoryStatsReaderTest, HandlesUnlimitedMemory) {
  NiceMock<MockFilesystem> mock_fs;

  // Mock fileExists to return true for V1 paths
  ON_CALL(mock_fs, fileExists(CgroupPaths::V2::getUsagePath())).WillByDefault(Return(false));
  ON_CALL(mock_fs, fileExists(CgroupPaths::V2::getLimitPath())).WillByDefault(Return(false));
  ON_CALL(mock_fs, fileExists(CgroupPaths::CGROUP_V1_BASE)).WillByDefault(Return(true));

  EXPECT_CALL(mock_fs, fileReadToEnd(CgroupPaths::V1::getLimitPath()))
      .WillOnce(Return(absl::StatusOr<std::string>("max")));

  auto reader = CgroupV1StatsReader::create(mock_fs);
  ASSERT_NE(reader, nullptr);
  EXPECT_EQ(reader->getMemoryLimit(), CgroupMemoryStatsReader::UNLIMITED_MEMORY);
}

// Tests that the stats reader handles unlimited memory with different formats.
TEST(CgroupMemoryStatsReaderTest, HandlesUnlimitedMemoryFormats) {
  NiceMock<MockFilesystem> mock_fs;

  // Mock fileExists to return true for V1 paths
  ON_CALL(mock_fs, fileExists(CgroupPaths::V2::getUsagePath())).WillByDefault(Return(false));
  ON_CALL(mock_fs, fileExists(CgroupPaths::V2::getLimitPath())).WillByDefault(Return(false));
  ON_CALL(mock_fs, fileExists(CgroupPaths::CGROUP_V1_BASE)).WillByDefault(Return(true));

  EXPECT_CALL(mock_fs, fileReadToEnd(CgroupPaths::V1::getLimitPath()))
      .WillOnce(Return(absl::StatusOr<std::string>("-1")));

  auto reader = CgroupV1StatsReader::create(mock_fs);
  ASSERT_NE(reader, nullptr);
  EXPECT_EQ(reader->getMemoryLimit(), CgroupMemoryStatsReader::UNLIMITED_MEMORY);
}

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

  NiceMock<MockFilesystem> mock_fs;
  EXPECT_CALL(mock_fs, fileReadToEnd(usage_path))
      .WillOnce(Return(absl::StatusOr<std::string>("1000\n")));
  EXPECT_CALL(mock_fs, fileReadToEnd(limit_path))
      .WillOnce(Return(absl::StatusOr<std::string>("2000\n")));

  TestCgroupV1StatsReader stats_reader(mock_fs, usage_path, limit_path);
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

  NiceMock<MockFilesystem> mock_fs;
  EXPECT_CALL(mock_fs, fileReadToEnd(usage_path))
      .WillOnce(Return(absl::StatusOr<std::string>("1500\n")));
  EXPECT_CALL(mock_fs, fileReadToEnd(limit_path))
      .WillOnce(Return(absl::StatusOr<std::string>("3000\n")));

  TestCgroupV2StatsReader stats_reader(mock_fs, usage_path, limit_path);
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

  NiceMock<MockFilesystem> mock_fs;
  EXPECT_CALL(mock_fs, fileReadToEnd(usage_path))
      .WillOnce(Return(absl::StatusOr<std::string>("1000\n")));
  EXPECT_CALL(mock_fs, fileReadToEnd(limit_path))
      .WillOnce(Return(absl::StatusOr<std::string>("max\n")));

  TestCgroupV2StatsReader stats_reader(mock_fs, usage_path, limit_path);
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

  NiceMock<MockFilesystem> mock_fs;
  EXPECT_CALL(mock_fs, fileReadToEnd(limit_path))
      .WillOnce(Return(absl::StatusOr<std::string>("-1\n")));

  TestCgroupV1StatsReader stats_reader(mock_fs, "dummy", limit_path);
  EXPECT_EQ(stats_reader.getMemoryLimit(), CgroupMemoryStatsReader::UNLIMITED_MEMORY);
}

// Tests that an exception is thrown when the memory stats file is missing.
TEST(CgroupMemoryStatsReaderTest, ThrowsOnMissingFile) {
  const std::string nonexistent_path = TestEnvironment::temporaryPath("nonexistent");
  NiceMock<MockFilesystem> mock_fs;
  EXPECT_CALL(mock_fs, fileReadToEnd(nonexistent_path))
      .WillOnce(Throw(EnvoyException("Unable to read memory stats file")));

  TestCgroupV1StatsReader stats_reader(mock_fs, nonexistent_path, "dummy");
  EXPECT_THROW_WITH_MESSAGE(stats_reader.getMemoryUsage(), EnvoyException,
                            "Unable to read memory stats file");
}

// Tests that an exception is thrown when the memory stats file contains invalid content.
TEST(CgroupMemoryStatsReaderTest, ThrowsOnInvalidContent) {
  const std::string invalid_path = TestEnvironment::temporaryPath("invalid");
  {
    AtomicFileUpdater file_updater(invalid_path);
    file_updater.update("not_a_number\n");
  }

  NiceMock<MockFilesystem> mock_fs;
  EXPECT_CALL(mock_fs, fileReadToEnd(invalid_path))
      .WillOnce(Return(absl::StatusOr<std::string>("not_a_number\n")));

  TestCgroupV1StatsReader stats_reader(mock_fs, invalid_path, "dummy");
  EXPECT_THROW(stats_reader.getMemoryUsage(), EnvoyException);
}

// Tests that an exception is thrown when the memory stats file is empty.
TEST(CgroupMemoryStatsReaderTest, ThrowsOnEmptyFile) {
  const std::string empty_path = TestEnvironment::temporaryPath("empty");
  {
    AtomicFileUpdater file_updater(empty_path);
    file_updater.update("");
  }

  NiceMock<MockFilesystem> mock_fs;
  EXPECT_CALL(mock_fs, fileReadToEnd(empty_path)).WillOnce(Return(absl::StatusOr<std::string>("")));

  TestCgroupV1StatsReader stats_reader(mock_fs, empty_path, "dummy");
  EXPECT_THROW_WITH_MESSAGE(stats_reader.getMemoryUsage(), EnvoyException,
                            fmt::format("Empty memory stats file at {}", empty_path));
}

// Tests that an exception is thrown when the memory stats file is unreadable.
TEST(CgroupMemoryStatsReaderTest, ThrowsOnUnreadableFile) {
  const std::string unreadable_path = TestEnvironment::temporaryPath("unreadable");
  {
    AtomicFileUpdater file_updater(unreadable_path);
    file_updater.update("100\n");
    chmod(unreadable_path.c_str(), 0000);
  }

  NiceMock<MockFilesystem> mock_fs;
  EXPECT_CALL(mock_fs, fileReadToEnd(unreadable_path))
      .WillOnce(Return(absl::InvalidArgumentError("Unable to open memory stats file")));

  TestCgroupV1StatsReader stats_reader(mock_fs, unreadable_path, "dummy");
  EXPECT_THROW_WITH_MESSAGE(stats_reader.getMemoryUsage(), EnvoyException,
                            fmt::format("Unable to read memory stats file at {}", unreadable_path));

  chmod(unreadable_path.c_str(), 0644);
}

// Tests that an exception is thrown when the memory stats file cannot be read.
TEST(CgroupMemoryStatsReaderTest, ThrowsOnReadError) {
  const std::string path = TestEnvironment::temporaryPath("read_error");
  {
    AtomicFileUpdater file_updater(path);
    file_updater.update("");
  }

  NiceMock<MockFilesystem> mock_fs;
  EXPECT_CALL(mock_fs, fileReadToEnd(path))
      .WillOnce(Return(absl::InvalidArgumentError("Unable to read memory stats from file")));

  TestCgroupV1StatsReader stats_reader(mock_fs, path, "dummy");
  EXPECT_THROW_WITH_MESSAGE(stats_reader.getMemoryUsage(), EnvoyException,
                            fmt::format("Unable to read memory stats file at {}", path));
}

TEST(CgroupMemoryStatsReaderTest, ThrowsOnWhitespaceOnlyFile) {
  const std::string whitespace_path = TestEnvironment::temporaryPath("whitespace");
  {
    AtomicFileUpdater file_updater(whitespace_path);
    file_updater.update("  \n\t  \n");
  }

  NiceMock<MockFilesystem> mock_fs;
  EXPECT_CALL(mock_fs, fileReadToEnd(whitespace_path))
      .WillOnce(Return(absl::StatusOr<std::string>("  \n\t  \n")));

  TestCgroupV1StatsReader stats_reader(mock_fs, whitespace_path, "dummy");
  EXPECT_THROW_WITH_MESSAGE(stats_reader.getMemoryUsage(), EnvoyException,
                            fmt::format("Empty memory stats file at {}", whitespace_path));
}

// Tests that factory creates V2 reader when V2 implementation is available.
TEST(CgroupMemoryStatsReaderTest, CreateReturnsV2ReaderWhenV2Available) {
  NiceMock<MockFilesystem> mock_fs;
  ON_CALL(mock_fs, fileExists).WillByDefault(Return(false));

  EXPECT_CALL(mock_fs, fileExists(CgroupPaths::V2::getUsagePath())).WillOnce(Return(true));
  EXPECT_CALL(mock_fs, fileExists(CgroupPaths::V2::getLimitPath())).WillOnce(Return(true));

  auto reader = CgroupMemoryStatsReader::create(mock_fs);
  EXPECT_NE(dynamic_cast<CgroupV2StatsReader*>(reader.get()), nullptr);
}

// Tests that factory falls back to V1 reader when V2 is not available.
TEST(CgroupMemoryStatsReaderTest, CreateReturnsV1ReaderWhenV1Available) {
  NiceMock<MockFilesystem> mock_fs;
  ON_CALL(mock_fs, fileExists).WillByDefault(Return(false));

  EXPECT_CALL(mock_fs, fileExists(CgroupPaths::V2::getUsagePath())).WillOnce(Return(false));
  EXPECT_CALL(mock_fs, fileExists(CgroupPaths::CGROUP_V1_BASE)).WillOnce(Return(true));

  auto reader = CgroupMemoryStatsReader::create(mock_fs);
  EXPECT_NE(dynamic_cast<CgroupV1StatsReader*>(reader.get()), nullptr);
}

// Tests that factory throws when no cgroup implementation is available.
TEST(CgroupMemoryStatsReaderTest, CreateThrowsWhenNoImplementationAvailable) {
  NiceMock<MockFilesystem> mock_fs;
  ON_CALL(mock_fs, fileExists).WillByDefault(Return(false));

  EXPECT_CALL(mock_fs, fileExists(CgroupPaths::V2::getUsagePath())).WillOnce(Return(false));
  EXPECT_CALL(mock_fs, fileExists(CgroupPaths::CGROUP_V1_BASE)).WillOnce(Return(false));

  EXPECT_THROW_WITH_MESSAGE(CgroupMemoryStatsReader::create(mock_fs), EnvoyException,
                            "No supported cgroup memory implementation found");
}

// Tests that V1 reader returns correct paths
TEST(CgroupMemoryStatsReaderTest, V1ReaderReturnsPaths) {
  NiceMock<MockFilesystem> mock_fs;
  TestableV1StatsReader stats_reader(mock_fs);
  EXPECT_EQ(stats_reader.getMemoryUsagePath(), CgroupPaths::V1::getUsagePath());
  EXPECT_EQ(stats_reader.getMemoryLimitPath(), CgroupPaths::V1::getLimitPath());
}

// Tests that V2 reader returns correct paths
TEST(CgroupMemoryStatsReaderTest, V2ReaderReturnsPaths) {
  NiceMock<MockFilesystem> mock_fs;
  TestableV2StatsReader stats_reader(mock_fs);
  EXPECT_EQ(stats_reader.getMemoryUsagePath(), CgroupPaths::V2::getUsagePath());
  EXPECT_EQ(stats_reader.getMemoryLimitPath(), CgroupPaths::V2::getLimitPath());
}

} // namespace
} // namespace CgroupMemory
} // namespace ResourceMonitors
} // namespace Extensions
} // namespace Envoy
