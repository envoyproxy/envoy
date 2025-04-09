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

TEST(CgroupMemoryStatsReaderTest, ThrowsOnMissingFile) {
  const std::string nonexistent_path = TestEnvironment::temporaryPath("nonexistent");
  TestCgroupV1StatsReader stats_reader(nonexistent_path, "dummy");
  EXPECT_THROW_WITH_MESSAGE(stats_reader.getMemoryUsage(), EnvoyException,
                           fmt::format("Unable to open memory stats file at {}", nonexistent_path));
}

TEST(CgroupMemoryStatsReaderTest, ThrowsOnInvalidContent) {
  const std::string invalid_path = TestEnvironment::temporaryPath("invalid");
  {
    AtomicFileUpdater file_updater(invalid_path);
    file_updater.update("not_a_number\n");
  }

  TestCgroupV1StatsReader stats_reader(invalid_path, "dummy");
  EXPECT_THROW(stats_reader.getMemoryUsage(), EnvoyException);
}

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

} // namespace
} // namespace CgroupMemory
} // namespace ResourceMonitors
} // namespace Extensions
} // namespace Envoy
