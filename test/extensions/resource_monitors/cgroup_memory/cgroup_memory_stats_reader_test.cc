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

TEST(CgroupMemoryStatsReaderTest, ReadsMemoryUsage) {
  NiceMock<Filesystem::MockInstance> mock_fs;

  ON_CALL(mock_fs, fileExists(CgroupPaths::V2::getUsagePath())).WillByDefault(Return(false));
  ON_CALL(mock_fs, fileExists(CgroupPaths::V2::getLimitPath())).WillByDefault(Return(false));
  ON_CALL(mock_fs, fileExists(CgroupPaths::V1::getBasePath())).WillByDefault(Return(true));

  EXPECT_CALL(mock_fs, fileReadToEnd(CgroupPaths::V1::getUsagePath()))
      .WillOnce(Return(absl::StatusOr<std::string>("500")));

  auto reader_or = CgroupMemoryStatsReader::create(mock_fs);
  ASSERT_TRUE(reader_or.ok());
  auto result = reader_or.value()->getMemoryUsage();
  ASSERT_TRUE(result.ok());
  EXPECT_EQ(result.value(), 500);
}

TEST(CgroupMemoryStatsReaderTest, ReadsMemoryLimit) {
  NiceMock<Filesystem::MockInstance> mock_fs;

  ON_CALL(mock_fs, fileExists(CgroupPaths::V2::getUsagePath())).WillByDefault(Return(false));
  ON_CALL(mock_fs, fileExists(CgroupPaths::V2::getLimitPath())).WillByDefault(Return(false));
  ON_CALL(mock_fs, fileExists(CgroupPaths::V1::getBasePath())).WillByDefault(Return(true));

  EXPECT_CALL(mock_fs, fileReadToEnd(CgroupPaths::V1::getLimitPath()))
      .WillOnce(Return(absl::StatusOr<std::string>("1000")));

  auto reader_or = CgroupMemoryStatsReader::create(mock_fs);
  ASSERT_TRUE(reader_or.ok());
  auto result = reader_or.value()->getMemoryLimit();
  ASSERT_TRUE(result.ok());
  EXPECT_EQ(result.value(), 1000);
}

TEST(CgroupMemoryStatsReaderTest, HandlesEmptyFiles) {
  NiceMock<Filesystem::MockInstance> mock_fs;

  ON_CALL(mock_fs, fileExists(CgroupPaths::V2::getUsagePath())).WillByDefault(Return(false));
  ON_CALL(mock_fs, fileExists(CgroupPaths::V2::getLimitPath())).WillByDefault(Return(false));
  ON_CALL(mock_fs, fileExists(CgroupPaths::V1::getBasePath())).WillByDefault(Return(true));

  EXPECT_CALL(mock_fs, fileReadToEnd(CgroupPaths::V1::getUsagePath()))
      .WillOnce(Return(absl::StatusOr<std::string>("")));

  auto reader_or = CgroupMemoryStatsReader::create(mock_fs);
  ASSERT_TRUE(reader_or.ok());
  auto result = reader_or.value()->getMemoryUsage();
  EXPECT_FALSE(result.ok());
  EXPECT_THAT(std::string(result.status().message()), ::testing::HasSubstr("Empty memory stats"));
}

TEST(CgroupMemoryStatsReaderTest, HandlesInvalidValues) {
  NiceMock<Filesystem::MockInstance> mock_fs;

  ON_CALL(mock_fs, fileExists(CgroupPaths::V2::getUsagePath())).WillByDefault(Return(false));
  ON_CALL(mock_fs, fileExists(CgroupPaths::V2::getLimitPath())).WillByDefault(Return(false));
  ON_CALL(mock_fs, fileExists(CgroupPaths::V1::getBasePath())).WillByDefault(Return(true));

  EXPECT_CALL(mock_fs, fileReadToEnd(CgroupPaths::V1::getUsagePath()))
      .WillOnce(Return(absl::StatusOr<std::string>("invalid")));

  auto reader_or = CgroupMemoryStatsReader::create(mock_fs);
  ASSERT_TRUE(reader_or.ok());
  auto result = reader_or.value()->getMemoryUsage();
  EXPECT_FALSE(result.ok());
  EXPECT_THAT(std::string(result.status().message()),
              ::testing::HasSubstr("Unable to parse memory stats"));
}

TEST(CgroupMemoryStatsReaderTest, HandlesFileReadErrors) {
  NiceMock<Filesystem::MockInstance> mock_fs;

  ON_CALL(mock_fs, fileExists(CgroupPaths::V2::getUsagePath())).WillByDefault(Return(false));
  ON_CALL(mock_fs, fileExists(CgroupPaths::V2::getLimitPath())).WillByDefault(Return(false));
  ON_CALL(mock_fs, fileExists(CgroupPaths::V1::getBasePath())).WillByDefault(Return(true));

  EXPECT_CALL(mock_fs, fileReadToEnd(CgroupPaths::V1::getUsagePath()))
      .WillOnce(Return(absl::InvalidArgumentError("Failed to read file")));

  auto reader_or = CgroupMemoryStatsReader::create(mock_fs);
  ASSERT_TRUE(reader_or.ok());
  auto result = reader_or.value()->getMemoryUsage();
  EXPECT_FALSE(result.ok());
  EXPECT_THAT(std::string(result.status().message()),
              ::testing::HasSubstr("Unable to read memory stats"));
}

TEST(CgroupMemoryStatsReaderTest, HandlesUnlimitedMemoryFormats) {
  NiceMock<Filesystem::MockInstance> mock_fs;

  ON_CALL(mock_fs, fileExists(CgroupPaths::V2::getUsagePath())).WillByDefault(Return(false));
  ON_CALL(mock_fs, fileExists(CgroupPaths::V2::getLimitPath())).WillByDefault(Return(false));
  ON_CALL(mock_fs, fileExists(CgroupPaths::V1::getBasePath())).WillByDefault(Return(true));

  // Test case 1: "max" format (V2 style)
  EXPECT_CALL(mock_fs, fileReadToEnd(CgroupPaths::V1::getLimitPath()))
      .WillOnce(Return(absl::StatusOr<std::string>("max")));

  auto reader_or = CgroupMemoryStatsReader::create(mock_fs);
  ASSERT_TRUE(reader_or.ok());
  auto result1 = reader_or.value()->getMemoryLimit();
  ASSERT_TRUE(result1.ok());
  EXPECT_EQ(result1.value(), CgroupMemoryStatsReader::UNLIMITED_MEMORY);

  // Test case 2: "-1" format (V1 style)
  EXPECT_CALL(mock_fs, fileReadToEnd(CgroupPaths::V1::getLimitPath()))
      .WillOnce(Return(absl::StatusOr<std::string>("-1")));

  reader_or = CgroupMemoryStatsReader::create(mock_fs);
  ASSERT_TRUE(reader_or.ok());
  auto result2 = reader_or.value()->getMemoryLimit();
  ASSERT_TRUE(result2.ok());
  EXPECT_EQ(result2.value(), CgroupMemoryStatsReader::UNLIMITED_MEMORY);
}

TEST(CgroupMemoryStatsReaderTest, ReadsV1MemoryStats) {
  NiceMock<Filesystem::MockInstance> mock_fs;
  EXPECT_CALL(mock_fs, fileReadToEnd("usage_path"))
      .WillOnce(Return(absl::StatusOr<std::string>("1000\n")));
  EXPECT_CALL(mock_fs, fileReadToEnd("limit_path"))
      .WillOnce(Return(absl::StatusOr<std::string>("2000\n")));

  TestCgroupV1StatsReader stats_reader(mock_fs, "usage_path", "limit_path");
  auto usage = stats_reader.getMemoryUsage();
  ASSERT_TRUE(usage.ok());
  EXPECT_EQ(usage.value(), 1000);
  auto limit = stats_reader.getMemoryLimit();
  ASSERT_TRUE(limit.ok());
  EXPECT_EQ(limit.value(), 2000);
}

TEST(CgroupMemoryStatsReaderTest, ReadsV2MemoryStats) {
  NiceMock<Filesystem::MockInstance> mock_fs;
  EXPECT_CALL(mock_fs, fileReadToEnd("usage_path"))
      .WillOnce(Return(absl::StatusOr<std::string>("1500\n")));
  EXPECT_CALL(mock_fs, fileReadToEnd("limit_path"))
      .WillOnce(Return(absl::StatusOr<std::string>("3000\n")));

  TestCgroupV2StatsReader stats_reader(mock_fs, "usage_path", "limit_path");
  auto usage = stats_reader.getMemoryUsage();
  ASSERT_TRUE(usage.ok());
  EXPECT_EQ(usage.value(), 1500);
  auto limit = stats_reader.getMemoryLimit();
  ASSERT_TRUE(limit.ok());
  EXPECT_EQ(limit.value(), 3000);
}

TEST(CgroupMemoryStatsReaderTest, HandlesV2MaxValue) {
  NiceMock<Filesystem::MockInstance> mock_fs;
  EXPECT_CALL(mock_fs, fileReadToEnd("usage_path"))
      .WillOnce(Return(absl::StatusOr<std::string>("1000\n")));
  EXPECT_CALL(mock_fs, fileReadToEnd("limit_path"))
      .WillOnce(Return(absl::StatusOr<std::string>("max\n")));

  TestCgroupV2StatsReader stats_reader(mock_fs, "usage_path", "limit_path");
  auto usage = stats_reader.getMemoryUsage();
  ASSERT_TRUE(usage.ok());
  EXPECT_EQ(usage.value(), 1000);
  auto limit = stats_reader.getMemoryLimit();
  ASSERT_TRUE(limit.ok());
  EXPECT_EQ(limit.value(), std::numeric_limits<uint64_t>::max());
}

TEST(CgroupMemoryStatsReaderTest, HandlesV1UnlimitedValue) {
  NiceMock<Filesystem::MockInstance> mock_fs;
  EXPECT_CALL(mock_fs, fileReadToEnd("limit_path"))
      .WillOnce(Return(absl::StatusOr<std::string>("-1\n")));

  TestCgroupV1StatsReader stats_reader(mock_fs, "dummy", "limit_path");
  auto result = stats_reader.getMemoryLimit();
  ASSERT_TRUE(result.ok());
  EXPECT_EQ(result.value(), CgroupMemoryStatsReader::UNLIMITED_MEMORY);
}

TEST(CgroupMemoryStatsReaderTest, HandlesErrorScenarios) {
  NiceMock<Filesystem::MockInstance> mock_fs;

  ON_CALL(mock_fs, fileExists(CgroupPaths::V2::getUsagePath())).WillByDefault(Return(false));
  ON_CALL(mock_fs, fileExists(CgroupPaths::V2::getLimitPath())).WillByDefault(Return(false));
  ON_CALL(mock_fs, fileExists(CgroupPaths::V1::getBasePath())).WillByDefault(Return(true));

  // Test case 1: File read returns error status
  {
    const std::string path = "missing_file";
    EXPECT_CALL(mock_fs, fileReadToEnd(path))
        .WillOnce(Return(absl::InvalidArgumentError("File not found")));
    TestCgroupV1StatsReader stats_reader1(mock_fs, path, "dummy");
    auto result = stats_reader1.getMemoryUsage();
    EXPECT_FALSE(result.ok());
    EXPECT_THAT(std::string(result.status().message()),
                ::testing::HasSubstr("Unable to read memory stats file"));
  }

  // Test case 2: Invalid content
  {
    EXPECT_CALL(mock_fs, fileReadToEnd("invalid_path"))
        .WillOnce(Return(absl::StatusOr<std::string>("not_a_number\n")));
    TestCgroupV1StatsReader stats_reader2(mock_fs, "invalid_path", "dummy");
    auto result = stats_reader2.getMemoryUsage();
    EXPECT_FALSE(result.ok());
    EXPECT_THAT(std::string(result.status().message()),
                ::testing::HasSubstr("Unable to parse memory stats"));
  }

  // Test case 3: Empty file
  {
    const std::string empty_path = "empty_file";
    EXPECT_CALL(mock_fs, fileReadToEnd(empty_path))
        .WillOnce(Return(absl::StatusOr<std::string>("")));
    TestCgroupV1StatsReader stats_reader3(mock_fs, empty_path, "dummy");
    auto result = stats_reader3.getMemoryUsage();
    EXPECT_FALSE(result.ok());
    EXPECT_THAT(std::string(result.status().message()),
                ::testing::HasSubstr(fmt::format("Empty memory stats file at {}", empty_path)));
  }

  // Test case 4: Unreadable file
  {
    const std::string unreadable_path = "unreadable_file";
    EXPECT_CALL(mock_fs, fileReadToEnd(unreadable_path))
        .WillOnce(Return(absl::InvalidArgumentError("Unable to open memory stats file")));
    TestCgroupV1StatsReader stats_reader4(mock_fs, unreadable_path, "dummy");
    auto result = stats_reader4.getMemoryUsage();
    EXPECT_FALSE(result.ok());
    EXPECT_THAT(std::string(result.status().message()),
                ::testing::HasSubstr(
                    fmt::format("Unable to read memory stats file at {}", unreadable_path)));
  }

  // Test case 5: Read error
  {
    const std::string path = "read_error_file";
    EXPECT_CALL(mock_fs, fileReadToEnd(path))
        .WillOnce(Return(absl::InvalidArgumentError("Unable to read memory stats from file")));
    TestCgroupV1StatsReader stats_reader5(mock_fs, path, "dummy");
    auto result = stats_reader5.getMemoryUsage();
    EXPECT_FALSE(result.ok());
    EXPECT_THAT(std::string(result.status().message()),
                ::testing::HasSubstr(fmt::format("Unable to read memory stats file at {}", path)));
  }

  // Test case 6: Whitespace-only file
  {
    const std::string whitespace_path = "whitespace_file";
    EXPECT_CALL(mock_fs, fileReadToEnd(whitespace_path))
        .WillOnce(Return(absl::StatusOr<std::string>("  \n\t  \n")));
    TestCgroupV1StatsReader stats_reader6(mock_fs, whitespace_path, "dummy");
    auto result = stats_reader6.getMemoryUsage();
    EXPECT_FALSE(result.ok());
    EXPECT_THAT(
        std::string(result.status().message()),
        ::testing::HasSubstr(fmt::format("Empty memory stats file at {}", whitespace_path)));
  }
}

TEST(CgroupMemoryStatsReaderTest, CreateReturnsV2ReaderWhenV2Available) {
  NiceMock<Filesystem::MockInstance> mock_fs;
  ON_CALL(mock_fs, fileExists).WillByDefault(Return(false));

  EXPECT_CALL(mock_fs, fileExists(CgroupPaths::V2::getUsagePath())).WillOnce(Return(true));
  EXPECT_CALL(mock_fs, fileExists(CgroupPaths::V2::getLimitPath())).WillOnce(Return(true));

  auto reader_or = CgroupMemoryStatsReader::create(mock_fs);
  ASSERT_TRUE(reader_or.ok());
  EXPECT_NE(dynamic_cast<CgroupV2StatsReader*>(reader_or.value().get()), nullptr);
}

TEST(CgroupMemoryStatsReaderTest, CreateReturnsV1ReaderWhenV1Available) {
  NiceMock<Filesystem::MockInstance> mock_fs;
  ON_CALL(mock_fs, fileExists).WillByDefault(Return(false));

  EXPECT_CALL(mock_fs, fileExists(CgroupPaths::V2::getUsagePath())).WillOnce(Return(false));
  EXPECT_CALL(mock_fs, fileExists(CgroupPaths::V1::getBasePath())).WillOnce(Return(true));

  auto reader_or = CgroupMemoryStatsReader::create(mock_fs);
  ASSERT_TRUE(reader_or.ok());
  EXPECT_NE(dynamic_cast<CgroupV1StatsReader*>(reader_or.value().get()), nullptr);
}

TEST(CgroupMemoryStatsReaderTest, CreateReturnsErrorWhenNoImplementationAvailable) {
  NiceMock<Filesystem::MockInstance> mock_fs;
  ON_CALL(mock_fs, fileExists).WillByDefault(Return(false));

  EXPECT_CALL(mock_fs, fileExists(CgroupPaths::V2::getUsagePath())).WillOnce(Return(false));
  EXPECT_CALL(mock_fs, fileExists(CgroupPaths::V1::getBasePath())).WillOnce(Return(false));

  auto result = CgroupMemoryStatsReader::create(mock_fs);
  EXPECT_FALSE(result.ok());
  EXPECT_THAT(std::string(result.status().message()),
              ::testing::Eq("No supported cgroup memory implementation found"));
}

TEST(CgroupMemoryStatsReaderTest, ReadersReturnCorrectPaths) {
  NiceMock<Filesystem::MockInstance> mock_fs;

  TestableV1StatsReader v1_stats_reader(mock_fs);
  EXPECT_EQ(v1_stats_reader.getMemoryUsagePath(), CgroupPaths::V1::getUsagePath());
  EXPECT_EQ(v1_stats_reader.getMemoryLimitPath(), CgroupPaths::V1::getLimitPath());

  TestableV2StatsReader v2_stats_reader(mock_fs);
  EXPECT_EQ(v2_stats_reader.getMemoryUsagePath(), CgroupPaths::V2::getUsagePath());
  EXPECT_EQ(v2_stats_reader.getMemoryLimitPath(), CgroupPaths::V2::getLimitPath());
}

TEST(CgroupMemoryStatsReaderTest, CreateV1Reader) {
  NiceMock<Filesystem::MockInstance> mock_fs;
  ON_CALL(mock_fs, fileExists(testing::_)).WillByDefault(Return(false));

  EXPECT_CALL(mock_fs, fileExists(CgroupPaths::V2::getUsagePath())).WillOnce(Return(false));
  EXPECT_CALL(mock_fs, fileExists(CgroupPaths::V1::getBasePath())).WillOnce(Return(true));

  auto reader_or = CgroupMemoryStatsReader::create(mock_fs);
  ASSERT_TRUE(reader_or.ok());
  EXPECT_NE(reader_or.value(), nullptr);
  EXPECT_NE(dynamic_cast<CgroupV1StatsReader*>(reader_or.value().get()), nullptr);
}

TEST(CgroupMemoryStatsReaderTest, CreateV2Reader) {
  NiceMock<Filesystem::MockInstance> mock_fs;
  ON_CALL(mock_fs, fileExists(testing::_)).WillByDefault(Return(false));

  EXPECT_CALL(mock_fs, fileExists(CgroupPaths::V2::getUsagePath())).WillOnce(Return(true));
  EXPECT_CALL(mock_fs, fileExists(CgroupPaths::V2::getLimitPath())).WillOnce(Return(true));

  auto reader_or = CgroupMemoryStatsReader::create(mock_fs);
  ASSERT_TRUE(reader_or.ok());
  EXPECT_NE(reader_or.value(), nullptr);
  EXPECT_NE(dynamic_cast<CgroupV2StatsReader*>(reader_or.value().get()), nullptr);
}

TEST(CgroupMemoryStatsReaderTest, SubclassesOnlyImplementPathMethods) {
  NiceMock<Filesystem::MockInstance> mock_fs;

  TestCgroupV1StatsReader v1_reader(mock_fs, "v1_usage", "v1_limit");
  TestCgroupV2StatsReader v2_reader(mock_fs, "v2_usage", "v2_limit");

  EXPECT_EQ(v1_reader.getMemoryUsagePath(), "v1_usage");
  EXPECT_EQ(v1_reader.getMemoryLimitPath(), "v1_limit");
  EXPECT_EQ(v2_reader.getMemoryUsagePath(), "v2_usage");
  EXPECT_EQ(v2_reader.getMemoryLimitPath(), "v2_limit");

  EXPECT_CALL(mock_fs, fileReadToEnd("v1_usage"))
      .WillOnce(Return(absl::StatusOr<std::string>("1000\n")));
  EXPECT_CALL(mock_fs, fileReadToEnd("v1_limit"))
      .WillOnce(Return(absl::StatusOr<std::string>("2000\n")));

  auto usage = v1_reader.getMemoryUsage();
  ASSERT_TRUE(usage.ok());
  EXPECT_EQ(usage.value(), 1000);
  auto limit = v1_reader.getMemoryLimit();
  ASSERT_TRUE(limit.ok());
  EXPECT_EQ(limit.value(), 2000);
}

} // namespace
} // namespace CgroupMemory
} // namespace ResourceMonitors
} // namespace Extensions
} // namespace Envoy
