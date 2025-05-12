#include "source/extensions/resource_monitors/cgroup_memory/cgroup_memory_stats_reader.h"

#include "test/mocks/filesystem/mocks.h"
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
  NiceMock<Filesystem::MockInstance> mock_fs;

  // Mock fileExists to return true for V1 paths
  ON_CALL(mock_fs, fileExists(CgroupPaths::V2::getUsagePath())).WillByDefault(Return(false));
  ON_CALL(mock_fs, fileExists(CgroupPaths::V2::getLimitPath())).WillByDefault(Return(false));
  ON_CALL(mock_fs, fileExists(CgroupPaths::V1::getBasePath())).WillByDefault(Return(true));

  EXPECT_CALL(mock_fs, fileReadToEnd(CgroupPaths::V1::getUsagePath()))
      .WillOnce(Return(absl::StatusOr<std::string>("500")));

  auto reader = CgroupV1StatsReader::create(mock_fs);
  ASSERT_NE(reader, nullptr);
  EXPECT_EQ(reader->getMemoryUsage(), 500);
}

// Tests that the stats reader correctly reads memory limit from a file.
TEST(CgroupMemoryStatsReaderTest, ReadsMemoryLimit) {
  NiceMock<Filesystem::MockInstance> mock_fs;

  // Mock fileExists to return true for V1 paths
  ON_CALL(mock_fs, fileExists(CgroupPaths::V2::getUsagePath())).WillByDefault(Return(false));
  ON_CALL(mock_fs, fileExists(CgroupPaths::V2::getLimitPath())).WillByDefault(Return(false));
  ON_CALL(mock_fs, fileExists(CgroupPaths::V1::getBasePath())).WillByDefault(Return(true));

  EXPECT_CALL(mock_fs, fileReadToEnd(CgroupPaths::V1::getLimitPath()))
      .WillOnce(Return(absl::StatusOr<std::string>("1000")));

  auto reader = CgroupV1StatsReader::create(mock_fs);
  ASSERT_NE(reader, nullptr);
  EXPECT_EQ(reader->getMemoryLimit(), 1000);
}

// Tests that the stats reader handles empty files.
TEST(CgroupMemoryStatsReaderTest, HandlesEmptyFiles) {
  NiceMock<Filesystem::MockInstance> mock_fs;

  // Mock fileExists to return true for V1 paths
  ON_CALL(mock_fs, fileExists(CgroupPaths::V2::getUsagePath())).WillByDefault(Return(false));
  ON_CALL(mock_fs, fileExists(CgroupPaths::V2::getLimitPath())).WillByDefault(Return(false));
  ON_CALL(mock_fs, fileExists(CgroupPaths::V1::getBasePath())).WillByDefault(Return(true));

  EXPECT_CALL(mock_fs, fileReadToEnd(CgroupPaths::V1::getUsagePath()))
      .WillOnce(Return(absl::StatusOr<std::string>("")));

  auto reader = CgroupV1StatsReader::create(mock_fs);
  ASSERT_NE(reader, nullptr);
  EXPECT_THROW(reader->getMemoryUsage(), EnvoyException);
}

// Tests that the stats reader handles invalid values.
TEST(CgroupMemoryStatsReaderTest, HandlesInvalidValues) {
  NiceMock<Filesystem::MockInstance> mock_fs;

  // Mock fileExists to return true for V1 paths
  ON_CALL(mock_fs, fileExists(CgroupPaths::V2::getUsagePath())).WillByDefault(Return(false));
  ON_CALL(mock_fs, fileExists(CgroupPaths::V2::getLimitPath())).WillByDefault(Return(false));
  ON_CALL(mock_fs, fileExists(CgroupPaths::V1::getBasePath())).WillByDefault(Return(true));

  EXPECT_CALL(mock_fs, fileReadToEnd(CgroupPaths::V1::getUsagePath()))
      .WillOnce(Return(absl::StatusOr<std::string>("invalid")));

  auto reader = CgroupV1StatsReader::create(mock_fs);
  ASSERT_NE(reader, nullptr);
  EXPECT_THROW(reader->getMemoryUsage(), EnvoyException);
}

// Tests that the stats reader handles file read errors.
TEST(CgroupMemoryStatsReaderTest, HandlesFileReadErrors) {
  NiceMock<Filesystem::MockInstance> mock_fs;

  // Mock fileExists to return true for V1 paths
  ON_CALL(mock_fs, fileExists(CgroupPaths::V2::getUsagePath())).WillByDefault(Return(false));
  ON_CALL(mock_fs, fileExists(CgroupPaths::V2::getLimitPath())).WillByDefault(Return(false));
  ON_CALL(mock_fs, fileExists(CgroupPaths::V1::getBasePath())).WillByDefault(Return(true));

  EXPECT_CALL(mock_fs, fileReadToEnd(CgroupPaths::V1::getUsagePath()))
      .WillOnce(Return(absl::InvalidArgumentError("Failed to read file")));

  auto reader = CgroupV1StatsReader::create(mock_fs);
  ASSERT_NE(reader, nullptr);
  EXPECT_THROW(reader->getMemoryUsage(), EnvoyException);
}

// Tests that the stats reader handles unlimited memory in different formats
TEST(CgroupMemoryStatsReaderTest, HandlesUnlimitedMemoryFormats) {
  NiceMock<Filesystem::MockInstance> mock_fs;

  // Mock fileExists to return true for V1 paths
  ON_CALL(mock_fs, fileExists(CgroupPaths::V2::getUsagePath())).WillByDefault(Return(false));
  ON_CALL(mock_fs, fileExists(CgroupPaths::V2::getLimitPath())).WillByDefault(Return(false));
  ON_CALL(mock_fs, fileExists(CgroupPaths::V1::getBasePath())).WillByDefault(Return(true));

  // Test case 1: "max" format (V2 style)
  EXPECT_CALL(mock_fs, fileReadToEnd(CgroupPaths::V1::getLimitPath()))
      .WillOnce(Return(absl::StatusOr<std::string>("max")));

  auto reader = CgroupV1StatsReader::create(mock_fs);
  ASSERT_NE(reader, nullptr);
  EXPECT_EQ(reader->getMemoryLimit(), CgroupMemoryStatsReader::UNLIMITED_MEMORY);

  // Test case 2: "-1" format (V1 style)
  EXPECT_CALL(mock_fs, fileReadToEnd(CgroupPaths::V1::getLimitPath()))
      .WillOnce(Return(absl::StatusOr<std::string>("-1")));

  reader = CgroupV1StatsReader::create(mock_fs);
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

  NiceMock<Filesystem::MockInstance> mock_fs;
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

  NiceMock<Filesystem::MockInstance> mock_fs;
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

  NiceMock<Filesystem::MockInstance> mock_fs;
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

  NiceMock<Filesystem::MockInstance> mock_fs;
  EXPECT_CALL(mock_fs, fileReadToEnd(limit_path))
      .WillOnce(Return(absl::StatusOr<std::string>("-1\n")));

  TestCgroupV1StatsReader stats_reader(mock_fs, "dummy", limit_path);
  EXPECT_EQ(stats_reader.getMemoryLimit(), CgroupMemoryStatsReader::UNLIMITED_MEMORY);
}

// Tests that the stats reader handles various error scenarios
TEST(CgroupMemoryStatsReaderTest, HandlesErrorScenarios) {
  NiceMock<Filesystem::MockInstance> mock_fs;

  // Mock fileExists to return true for V1 paths
  ON_CALL(mock_fs, fileExists(CgroupPaths::V2::getUsagePath())).WillByDefault(Return(false));
  ON_CALL(mock_fs, fileExists(CgroupPaths::V2::getLimitPath())).WillByDefault(Return(false));
  ON_CALL(mock_fs, fileExists(CgroupPaths::V1::getBasePath())).WillByDefault(Return(true));

  // Test case 1: Missing file
  const std::string nonexistent_path = TestEnvironment::temporaryPath("nonexistent");
  EXPECT_CALL(mock_fs, fileReadToEnd(nonexistent_path))
      .WillOnce(Throw(EnvoyException("Unable to read memory stats file")));

  TestCgroupV1StatsReader stats_reader1(mock_fs, nonexistent_path, "dummy");
  EXPECT_THROW_WITH_MESSAGE(stats_reader1.getMemoryUsage(), EnvoyException,
                            "Unable to read memory stats file");

  // Test case 2: Invalid content
  const std::string invalid_path = TestEnvironment::temporaryPath("invalid");
  {
    AtomicFileUpdater file_updater(invalid_path);
    file_updater.update("not_a_number\n");
  }

  EXPECT_CALL(mock_fs, fileReadToEnd(invalid_path))
      .WillOnce(Return(absl::StatusOr<std::string>("not_a_number\n")));

  TestCgroupV1StatsReader stats_reader2(mock_fs, invalid_path, "dummy");
  EXPECT_THROW(stats_reader2.getMemoryUsage(), EnvoyException);

  // Test case 3: Empty file
  const std::string empty_path = TestEnvironment::temporaryPath("empty");
  {
    AtomicFileUpdater file_updater(empty_path);
    file_updater.update("");
  }

  EXPECT_CALL(mock_fs, fileReadToEnd(empty_path)).WillOnce(Return(absl::StatusOr<std::string>("")));

  TestCgroupV1StatsReader stats_reader3(mock_fs, empty_path, "dummy");
  EXPECT_THROW_WITH_MESSAGE(stats_reader3.getMemoryUsage(), EnvoyException,
                            fmt::format("Empty memory stats file at {}", empty_path));

  // Test case 4: Unreadable file
  const std::string unreadable_path = TestEnvironment::temporaryPath("unreadable");
  {
    AtomicFileUpdater file_updater(unreadable_path);
    file_updater.update("100\n");
    chmod(unreadable_path.c_str(), 0000);
  }

  EXPECT_CALL(mock_fs, fileReadToEnd(unreadable_path))
      .WillOnce(Return(absl::InvalidArgumentError("Unable to open memory stats file")));

  TestCgroupV1StatsReader stats_reader4(mock_fs, unreadable_path, "dummy");
  EXPECT_THROW_WITH_MESSAGE(stats_reader4.getMemoryUsage(), EnvoyException,
                            fmt::format("Unable to read memory stats file at {}", unreadable_path));

  chmod(unreadable_path.c_str(), 0644);

  // Test case 5: Read error
  const std::string path = TestEnvironment::temporaryPath("read_error");
  {
    AtomicFileUpdater file_updater(path);
    file_updater.update("");
  }

  EXPECT_CALL(mock_fs, fileReadToEnd(path))
      .WillOnce(Return(absl::InvalidArgumentError("Unable to read memory stats from file")));

  TestCgroupV1StatsReader stats_reader5(mock_fs, path, "dummy");
  EXPECT_THROW_WITH_MESSAGE(stats_reader5.getMemoryUsage(), EnvoyException,
                            fmt::format("Unable to read memory stats file at {}", path));

  // Test case 6: Whitespace-only file
  const std::string whitespace_path = TestEnvironment::temporaryPath("whitespace");
  {
    AtomicFileUpdater file_updater(whitespace_path);
    file_updater.update("  \n\t  \n");
  }

  EXPECT_CALL(mock_fs, fileReadToEnd(whitespace_path))
      .WillOnce(Return(absl::StatusOr<std::string>("  \n\t  \n")));

  TestCgroupV1StatsReader stats_reader6(mock_fs, whitespace_path, "dummy");
  EXPECT_THROW_WITH_MESSAGE(stats_reader6.getMemoryUsage(), EnvoyException,
                            fmt::format("Empty memory stats file at {}", whitespace_path));
}

// Tests that factory creates V2 reader when V2 implementation is available.
TEST(CgroupMemoryStatsReaderTest, CreateReturnsV2ReaderWhenV2Available) {
  NiceMock<Filesystem::MockInstance> mock_fs;
  ON_CALL(mock_fs, fileExists).WillByDefault(Return(false));

  EXPECT_CALL(mock_fs, fileExists(CgroupPaths::V2::getUsagePath())).WillOnce(Return(true));
  EXPECT_CALL(mock_fs, fileExists(CgroupPaths::V2::getLimitPath())).WillOnce(Return(true));

  auto reader = CgroupMemoryStatsReader::create(mock_fs);
  EXPECT_NE(dynamic_cast<CgroupV2StatsReader*>(reader.get()), nullptr);
}

// Tests that factory falls back to V1 reader when V2 is not available.
TEST(CgroupMemoryStatsReaderTest, CreateReturnsV1ReaderWhenV1Available) {
  NiceMock<Filesystem::MockInstance> mock_fs;
  ON_CALL(mock_fs, fileExists).WillByDefault(Return(false));

  EXPECT_CALL(mock_fs, fileExists(CgroupPaths::V2::getUsagePath())).WillOnce(Return(false));
  EXPECT_CALL(mock_fs, fileExists(CgroupPaths::V1::getBasePath())).WillOnce(Return(true));

  auto reader = CgroupMemoryStatsReader::create(mock_fs);
  EXPECT_NE(dynamic_cast<CgroupV1StatsReader*>(reader.get()), nullptr);
}

// Tests that factory throws when no cgroup implementation is available.
TEST(CgroupMemoryStatsReaderTest, CreateThrowsWhenNoImplementationAvailable) {
  NiceMock<Filesystem::MockInstance> mock_fs;
  ON_CALL(mock_fs, fileExists).WillByDefault(Return(false));

  EXPECT_CALL(mock_fs, fileExists(CgroupPaths::V2::getUsagePath())).WillOnce(Return(false));
  EXPECT_CALL(mock_fs, fileExists(CgroupPaths::V1::getBasePath())).WillOnce(Return(false));

  EXPECT_THROW_WITH_MESSAGE(CgroupMemoryStatsReader::create(mock_fs), EnvoyException,
                            "No supported cgroup memory implementation found");
}

// Tests that V1 and V2 readers return correct paths
TEST(CgroupMemoryStatsReaderTest, ReadersReturnCorrectPaths) {
  NiceMock<Filesystem::MockInstance> mock_fs;

  // Test V1 reader paths
  TestableV1StatsReader v1_stats_reader(mock_fs);
  EXPECT_EQ(v1_stats_reader.getMemoryUsagePath(), CgroupPaths::V1::getUsagePath());
  EXPECT_EQ(v1_stats_reader.getMemoryLimitPath(), CgroupPaths::V1::getLimitPath());

  // Test V2 reader paths
  TestableV2StatsReader v2_stats_reader(mock_fs);
  EXPECT_EQ(v2_stats_reader.getMemoryUsagePath(), CgroupPaths::V2::getUsagePath());
  EXPECT_EQ(v2_stats_reader.getMemoryLimitPath(), CgroupPaths::V2::getLimitPath());
}

TEST(CgroupMemoryStatsReaderTest, CreateV1Reader) {
  NiceMock<Filesystem::MockInstance> mock_fs;

  // Set up default behavior for all fileExists calls
  ON_CALL(mock_fs, fileExists(testing::_)).WillByDefault(Return(false));

  // First, isV2 will check V2 usage path and return false (short-circuit)
  EXPECT_CALL(mock_fs, fileExists(CgroupPaths::V2::getUsagePath())).WillOnce(Return(false));
  // Note: V2 limit path should not be checked due to short-circuit

  // Then, isV1 will check V1 base path and return true
  EXPECT_CALL(mock_fs, fileExists(CgroupPaths::V1::getBasePath())).WillOnce(Return(true));

  auto reader = CgroupMemoryStatsReader::create(mock_fs);
  EXPECT_NE(reader, nullptr);
  EXPECT_NE(dynamic_cast<CgroupV1StatsReader*>(reader.get()), nullptr);
}

TEST(CgroupMemoryStatsReaderTest, CreateV2Reader) {
  NiceMock<Filesystem::MockInstance> mock_fs;

  // Set up default behavior for all fileExists calls
  ON_CALL(mock_fs, fileExists(testing::_)).WillByDefault(Return(false));

  // isV2 will check V2 paths and return true
  EXPECT_CALL(mock_fs, fileExists(CgroupPaths::V2::getUsagePath())).WillOnce(Return(true));
  EXPECT_CALL(mock_fs, fileExists(CgroupPaths::V2::getLimitPath())).WillOnce(Return(true));

  // isV1 will not be called because isV2 returned true

  auto reader = CgroupMemoryStatsReader::create(mock_fs);
  EXPECT_NE(reader, nullptr);
  EXPECT_NE(dynamic_cast<CgroupV2StatsReader*>(reader.get()), nullptr);
}

// Tests that subclasses only implement path-related methods
TEST(CgroupMemoryStatsReaderTest, SubclassesOnlyImplementPathMethods) {
  NiceMock<Filesystem::MockInstance> mock_fs;

  // Create V1 and V2 readers
  TestCgroupV1StatsReader v1_reader(mock_fs, "v1_usage", "v1_limit");
  TestCgroupV2StatsReader v2_reader(mock_fs, "v2_usage", "v2_limit");

  // Verify that path methods return the correct paths
  EXPECT_EQ(v1_reader.getMemoryUsagePath(), "v1_usage");
  EXPECT_EQ(v1_reader.getMemoryLimitPath(), "v1_limit");
  EXPECT_EQ(v2_reader.getMemoryUsagePath(), "v2_usage");
  EXPECT_EQ(v2_reader.getMemoryLimitPath(), "v2_limit");

  // Verify that memory usage and limit methods are handled by base class
  EXPECT_CALL(mock_fs, fileReadToEnd("v1_usage"))
      .WillOnce(Return(absl::StatusOr<std::string>("1000\n")));
  EXPECT_CALL(mock_fs, fileReadToEnd("v1_limit"))
      .WillOnce(Return(absl::StatusOr<std::string>("2000\n")));

  EXPECT_EQ(v1_reader.getMemoryUsage(), 1000);
  EXPECT_EQ(v1_reader.getMemoryLimit(), 2000);
}

} // namespace
} // namespace CgroupMemory
} // namespace ResourceMonitors
} // namespace Extensions
} // namespace Envoy
