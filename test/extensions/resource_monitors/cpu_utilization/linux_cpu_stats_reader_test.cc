#include <chrono>
#include <cstdlib>

#include "source/extensions/resource_monitors/cpu_utilization/linux_cpu_stats_reader.h"
#include "source/server/resource_monitor_config_impl.h"

#include "test/mocks/event/mocks.h"
#include "test/mocks/filesystem/mocks.h"
#include "test/mocks/server/options.h"
#include "test/test_common/environment.h"

#include "absl/types/optional.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace ResourceMonitors {
namespace CpuUtilizationMonitor {
namespace {

using testing::Return;

// =============================================================================
// LinuxCpuStatsReader Tests (Host /proc/stat)
// =============================================================================

TEST(LinuxCpuStatsReader, ReadsCpuStats) {
  const std::string temp_path = TestEnvironment::temporaryPath("cpu_stats");
  AtomicFileUpdater file_updater(temp_path);
  const std::string contents = R"EOF(cpu  14987204 4857 3003536 11594988 53631 0 759314 2463 0 0
cpu0 1907532 599 369969 1398344 5970 0 121763 18 0 0
cpu1 1883161 620 375962 1448133 5963 0 85914 10 0 0
cpu2 1877318 610 376223 1458160 5713 0 81227 10 0 0
cpu3 1844673 653 373370 1493333 6063 0 80492 1124 0 0
cpu4 1879904 572 380089 1440757 7348 0 91022 10 0 0
cpu5 1873470 607 377632 1449005 6359 0 94092 8 0 0
cpu6 1878276 576 375458 1423527 7995 0 115756 8 0 0
cpu7 1842866 615 374829 1483725 8218 0 89044 1272 0 0
intr 1219233916 0 10 0 0 555 0 0 0 0 0 0 0 154 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 15 12 20859 24167 1125607 1182080 40577 52852415 108966441 51142451 43742777 52285969 56216800 52419266 95242197 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0
ctxt 5657586121
btime 1700654901
processes 34530
procs_running 7
procs_blocked 0
softirq 1714610175 0 8718679 72686 1588388544 32 0 293214 77920941 12627 39203452
)EOF";
  file_updater.update(contents);

  LinuxCpuStatsReader cpu_stats_reader(temp_path);
  CpuTimesBase cpu_times = cpu_stats_reader.getCpuTimes();

  EXPECT_EQ(cpu_times.work_time, 17995597);
  EXPECT_EQ(cpu_times.total_time, 29590585);
}

TEST(LinuxCpuStatsReader, CannotReadFile) {
  const std::string temp_path = TestEnvironment::temporaryPath("cpu_stats_not_exists");
  LinuxCpuStatsReader cpu_stats_reader(temp_path);
  CpuTimesBase cpu_times = cpu_stats_reader.getCpuTimes();
  EXPECT_FALSE(cpu_times.is_valid);
  EXPECT_EQ(cpu_times.work_time, 0);
  EXPECT_EQ(cpu_times.total_time, 0);
}

TEST(LinuxCpuStatsReader, UnexpectedFormatCpuLine) {
  const std::string temp_path = TestEnvironment::temporaryPath("cpu_stats_unexpected_format");
  AtomicFileUpdater file_updater(temp_path);
  const std::string contents = R"EOF(cpu0 1907532 599 369969 1398344 5970 0 121763 18 0 0
cpu1 1883161 620 375962 1448133 5963 0 85914 10 0 0
cpu  14987204 4857 3003536 11594988 53631 0 759314 2463 0 0
)EOF";
  file_updater.update(contents);

  LinuxCpuStatsReader cpu_stats_reader(temp_path);
  CpuTimesBase cpu_times = cpu_stats_reader.getCpuTimes();
  EXPECT_FALSE(cpu_times.is_valid);
  EXPECT_EQ(cpu_times.work_time, 0);
  EXPECT_EQ(cpu_times.total_time, 0);
}

TEST(LinuxCpuStatsReader, UnexpectedFormatMissingTokens) {
  const std::string temp_path = TestEnvironment::temporaryPath("cpu_stats_unexpected_format");
  AtomicFileUpdater file_updater(temp_path);
  const std::string contents = R"EOF(cpu  14987204 4857 3003536
cpu0 1907532 599 369969 1398344 5970 0 121763 18 0 0
cpu1 1883161 620 375962 1448133 5963 0 85914 10 0 0
)EOF";
  file_updater.update(contents);

  LinuxCpuStatsReader cpu_stats_reader(temp_path);
  CpuTimesBase cpu_times = cpu_stats_reader.getCpuTimes();
  EXPECT_FALSE(cpu_times.is_valid);
  EXPECT_EQ(cpu_times.work_time, 0);
  EXPECT_EQ(cpu_times.total_time, 0);
}

// =============================================================================
// LinuxContainerCpuStatsReaderTest - Cgroup V1
// =============================================================================

class LinuxContainerCpuStatsReaderTest : public testing::Test {
public:
  LinuxContainerCpuStatsReaderTest()
      : api_(Api::createApiForTest()),
        context_(dispatcher_, options_, *api_, ProtobufMessage::getStrictValidationVisitor()),
        cpu_allocated_path_(TestEnvironment::temporaryPath("cgroup_cpu_allocated_stats")),
        cpu_times_path_(TestEnvironment::temporaryPath("cgroup_cpu_times_stats")) {
    // Default sane values so tests only need to set what they care about
    setCpuAllocated("2000\n");
    setCpuTimes("1000\n");
  }

  TimeSource& timeSource() { return context_.api().timeSource(); }

  const std::string& cpuAllocatedPath() const { return cpu_allocated_path_; }
  void setCpuAllocated(const std::string& contents) {
    AtomicFileUpdater cpu_allocated(cpuAllocatedPath());
    cpu_allocated.update(contents);
  }

  const std::string& cpuTimesPath() const { return cpu_times_path_; }
  void setCpuTimes(const std::string& contents) {
    AtomicFileUpdater cpu_times(cpuTimesPath());
    cpu_times.update(contents);
  }

private:
  Event::MockDispatcher dispatcher_;
  Api::ApiPtr api_;
  Server::MockOptions options_;
  Server::Configuration::ResourceMonitorFactoryContextImpl context_;
  std::string cpu_allocated_path_;
  std::string cpu_times_path_;
};

TEST_F(LinuxContainerCpuStatsReaderTest, ReadsCgroupContainerStats) {
  TimeSource& test_time_source = timeSource();
  Api::ApiPtr api = Api::createApiForTest();
  setCpuAllocated("2000\n");
  setCpuTimes("1000\n");

  CgroupV1CpuStatsReader container_stats_reader(api->fileSystem(), test_time_source,
                                                cpuAllocatedPath(), cpuTimesPath());
  CpuTimesBase envoy_container_stats = container_stats_reader.getCpuTimes();

  const uint64_t current_monotonic_time = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                              test_time_source.monotonicTime().time_since_epoch())
                                              .count();
  const int64_t time_diff_ns = current_monotonic_time - envoy_container_stats.total_time;

  EXPECT_EQ(envoy_container_stats.work_time, 500);
  EXPECT_GT(time_diff_ns, 0);
}

TEST_F(LinuxContainerCpuStatsReaderTest, CannotReadFileCpuAllocated) {
  TimeSource& test_time_source = timeSource();
  Api::ApiPtr api = Api::createApiForTest();
  const std::string temp_path_cpu_allocated =
      TestEnvironment::temporaryPath("container_cpu_times_not_exists");

  CgroupV1CpuStatsReader container_stats_reader(api->fileSystem(), test_time_source,
                                                temp_path_cpu_allocated, cpuTimesPath());
  CpuTimesBase envoy_container_stats = container_stats_reader.getCpuTimes();
  EXPECT_FALSE(envoy_container_stats.is_valid);

  // Test that getUtilization also handles the error
  auto result = container_stats_reader.getUtilization();
  EXPECT_FALSE(result.ok());
  EXPECT_NE(result.status().message().find("Failed to read CPU times"), std::string::npos);
}

TEST_F(LinuxContainerCpuStatsReaderTest, CannotReadFileCpuTimes) {
  TimeSource& test_time_source = timeSource();
  Api::ApiPtr api = Api::createApiForTest();
  const std::string temp_path_cpu_times =
      TestEnvironment::temporaryPath("container_cpu_times_not_exists");

  CgroupV1CpuStatsReader container_stats_reader(api->fileSystem(), test_time_source,
                                                cpuAllocatedPath(), temp_path_cpu_times);
  CpuTimesBase envoy_container_stats = container_stats_reader.getCpuTimes();
  EXPECT_FALSE(envoy_container_stats.is_valid);
}

TEST_F(LinuxContainerCpuStatsReaderTest, UnexpectedFormatCpuAllocatedLine) {
  TimeSource& test_time_source = timeSource();
  Api::ApiPtr api = Api::createApiForTest();
  setCpuAllocated("notanumb3r\n");

  CgroupV1CpuStatsReader container_stats_reader(api->fileSystem(), test_time_source,
                                                cpuAllocatedPath(), cpuTimesPath());
  CpuTimesBase envoy_container_stats = container_stats_reader.getCpuTimes();
  EXPECT_FALSE(envoy_container_stats.is_valid);
}

TEST_F(LinuxContainerCpuStatsReaderTest, UnexpectedFormatCpuTimesLine) {
  TimeSource& test_time_source = timeSource();
  Api::ApiPtr api = Api::createApiForTest();
  setCpuTimes("notanumb3r\n");

  CgroupV1CpuStatsReader container_stats_reader(api->fileSystem(), test_time_source,
                                                cpuAllocatedPath(), cpuTimesPath());
  CpuTimesBase envoy_container_stats = container_stats_reader.getCpuTimes();
  EXPECT_FALSE(envoy_container_stats.is_valid);
}

TEST_F(LinuxContainerCpuStatsReaderTest, ZeroCpuTimes) {
  TimeSource& test_time_source = timeSource();
  Api::ApiPtr api = Api::createApiForTest();
  setCpuAllocated("2000\n");
  setCpuTimes("0\n");

  CgroupV1CpuStatsReader container_stats_reader(api->fileSystem(), test_time_source,
                                                cpuAllocatedPath(), cpuTimesPath());
  CpuTimesBase envoy_container_stats = container_stats_reader.getCpuTimes();

  EXPECT_TRUE(envoy_container_stats.is_valid);
  EXPECT_EQ(envoy_container_stats.work_time, 0);
}

TEST_F(LinuxContainerCpuStatsReaderTest, ZeroCpuAllocatedValue) {
  TimeSource& test_time_source = timeSource();
  Api::ApiPtr api = Api::createApiForTest();
  setCpuAllocated("0\n");
  setCpuTimes("1000\n");

  CgroupV1CpuStatsReader container_stats_reader(api->fileSystem(), test_time_source,
                                                cpuAllocatedPath(), cpuTimesPath());
  CpuTimesBase envoy_container_stats = container_stats_reader.getCpuTimes();

  EXPECT_FALSE(envoy_container_stats.is_valid);
}

TEST_F(LinuxContainerCpuStatsReaderTest, V1GetUtilizationFirstCallReturnsZero) {
  TimeSource& test_time_source = timeSource();
  Api::ApiPtr api = Api::createApiForTest();
  setCpuAllocated("2000\n");
  setCpuTimes("1000\n");

  CgroupV1CpuStatsReader container_stats_reader(api->fileSystem(), test_time_source,
                                                cpuAllocatedPath(), cpuTimesPath());
  auto result = container_stats_reader.getUtilization();

  ASSERT_TRUE(result.ok());
  EXPECT_DOUBLE_EQ(result.value(), 0.0);

  // Also test negative work_over_period error scenario (cpu_times decreased)
  setCpuTimes("500\n");
  result = container_stats_reader.getUtilization();
  EXPECT_FALSE(result.ok());
  EXPECT_NE(result.status().message().find("Work_over_period"), std::string::npos);
}

// =============================================================================
// LinuxContainerCpuStatsReaderV2Test - Cgroup V2
// =============================================================================

class LinuxContainerCpuStatsReaderV2Test : public testing::Test {
public:
  LinuxContainerCpuStatsReaderV2Test()
      : api_(Api::createApiForTest()),
        context_(dispatcher_, options_, *api_, ProtobufMessage::getStrictValidationVisitor()),
        v2_cpu_stat_path_(TestEnvironment::temporaryPath("cgroupv2_cpu_stat")),
        v2_cpu_max_path_(TestEnvironment::temporaryPath("cgroupv2_cpu_max")),
        v2_cpu_effective_path_(TestEnvironment::temporaryPath("cgroupv2_cpu_effective")) {}

  TimeSource& timeSource() { return context_.api().timeSource(); }

  const std::string& v2CpuStatPath() const { return v2_cpu_stat_path_; }
  void setV2CpuStat(const std::string& contents) {
    AtomicFileUpdater file(v2CpuStatPath());
    file.update(contents);
  }

  const std::string& v2CpuMaxPath() const { return v2_cpu_max_path_; }
  void setV2CpuMax(const std::string& contents) {
    AtomicFileUpdater file(v2CpuMaxPath());
    file.update(contents);
  }

  const std::string& v2CpuEffectivePath() const { return v2_cpu_effective_path_; }
  void setV2CpuEffective(const std::string& contents) {
    AtomicFileUpdater file(v2CpuEffectivePath());
    file.update(contents);
  }

private:
  Event::MockDispatcher dispatcher_;
  Api::ApiPtr api_;
  Server::MockOptions options_;
  Server::Configuration::ResourceMonitorFactoryContextImpl context_;
  std::string v2_cpu_stat_path_;
  std::string v2_cpu_max_path_;
  std::string v2_cpu_effective_path_;
};

// Happy path coverage for effective CPU list parsing and cpu.max parsing.
TEST_F(LinuxContainerCpuStatsReaderV2Test, ParsesEffectiveCpusAndCores) {
  TimeSource& test_time_source = timeSource();
  Api::ApiPtr api = Api::createApiForTest();

  const auto get_cpu_times = [&](absl::string_view stat, absl::string_view max,
                                 absl::string_view effective) {
    setV2CpuStat(std::string(stat));
    setV2CpuMax(std::string(max));
    setV2CpuEffective(std::string(effective));
    CgroupV2CpuStatsReader reader(api->fileSystem(), test_time_source, v2CpuStatPath(),
                                  v2CpuMaxPath(), v2CpuEffectivePath());
    return reader.getCpuTimes();
  };

  struct Case {
    absl::string_view stat;
    absl::string_view max;
    absl::string_view effective;
    double expected_effective_cores;
  };

  const Case cases[] = {
      {"usage_usec 500000\n", "200000 100000\n", "0-3\n", 2.0},  // range, quota < N
      {"usage_usec 750000\n", "50000 100000\n", "0\n", 0.5},     // single CPU
      {"usage_usec 500000\n", "max 100000\n", "0-2,4\n", 4.0},   // mixed list, max quota
      {"usage_usec 500000\n", "800000 100000\n", "0-3\n", 4.0},  // quota > N
      {"usage_usec 100000\n", "25000 100000\n", "0-3\n", 0.25},  // fractional quota
      {"usage_usec 500000\n", "max 100000\n", "0-3,5-7\n", 7.0}, // multiple ranges
      {"usage_usec 500000\n", "max 100000\n", "0,2,4\n", 3.0},   // multiple singles
  };

  for (const auto& test_case : cases) {
    CpuTimesV2 cpu_times = get_cpu_times(test_case.stat, test_case.max, test_case.effective);
    EXPECT_TRUE(cpu_times.is_valid);
    EXPECT_DOUBLE_EQ(cpu_times.effective_cores, test_case.expected_effective_cores);
  }
}

// Error: Missing usage_usec in cpu.stat
TEST_F(LinuxContainerCpuStatsReaderV2Test, MissingUsageUsecInCpuStat) {
  TimeSource& test_time_source = timeSource();
  Api::ApiPtr api = Api::createApiForTest();
  setV2CpuStat("user_usec 300000\nsystem_usec 200000\n"); // No usage_usec
  setV2CpuMax("200000 100000\n");
  setV2CpuEffective("0-3\n");

  CgroupV2CpuStatsReader container_stats_reader(
      api->fileSystem(), test_time_source, v2CpuStatPath(), v2CpuMaxPath(), v2CpuEffectivePath());
  CpuTimesV2 envoy_container_stats = container_stats_reader.getCpuTimes();

  EXPECT_FALSE(envoy_container_stats.is_valid);
}

// Error: Invalid usage_usec format
TEST_F(LinuxContainerCpuStatsReaderV2Test, InvalidUsageUsecFormat) {
  TimeSource& test_time_source = timeSource();
  Api::ApiPtr api = Api::createApiForTest();
  setV2CpuStat("usage_usec notanumber\n");
  setV2CpuMax("200000 100000\n");
  setV2CpuEffective("0-3\n");

  CgroupV2CpuStatsReader container_stats_reader(
      api->fileSystem(), test_time_source, v2CpuStatPath(), v2CpuMaxPath(), v2CpuEffectivePath());
  CpuTimesV2 envoy_container_stats = container_stats_reader.getCpuTimes();

  EXPECT_FALSE(envoy_container_stats.is_valid);
}

// Error: Invalid cpuset.cpus.effective formats
TEST_F(LinuxContainerCpuStatsReaderV2Test, InvalidCpuEffectiveFormats) {
  TimeSource& test_time_source = timeSource();
  Api::ApiPtr api = Api::createApiForTest();
  setV2CpuStat("usage_usec 500000\n");
  setV2CpuMax("200000 100000\n");

  const absl::string_view invalid_effective[] = {
      "notanumber\n", // non-numeric token
      "-1\n",         // negative single CPU
      "0-abc\n",      // non-numeric range
      "-1-3\n",       // negative range start
      "5-2\n",        // range start > end
      "",             // empty list
  };

  for (const auto& effective : invalid_effective) {
    setV2CpuEffective(std::string(effective));
    CgroupV2CpuStatsReader reader(api->fileSystem(), test_time_source, v2CpuStatPath(),
                                  v2CpuMaxPath(), v2CpuEffectivePath());
    CpuTimesV2 cpu_times = reader.getCpuTimes();
    EXPECT_FALSE(cpu_times.is_valid);
  }

  // getUtilization should surface the same error path for an invalid effective CPU list.
  setV2CpuEffective("notanumber\n");
  CgroupV2CpuStatsReader reader(api->fileSystem(), test_time_source, v2CpuStatPath(),
                                v2CpuMaxPath(), v2CpuEffectivePath());
  auto result = reader.getUtilization();
  EXPECT_FALSE(result.ok());
  EXPECT_NE(result.status().message().find("Failed to read CPU times"), std::string::npos);
}

// Error: Invalid cpu.max file formats
TEST_F(LinuxContainerCpuStatsReaderV2Test, InvalidCpuMaxFormats) {
  TimeSource& test_time_source = timeSource();
  Api::ApiPtr api = Api::createApiForTest();
  setV2CpuStat("usage_usec 500000\n");
  setV2CpuEffective("0-3\n");

  // Test 1: Unexpected format - missing second value
  setV2CpuMax("200000\n");
  CgroupV2CpuStatsReader container_stats_reader1(
      api->fileSystem(), test_time_source, v2CpuStatPath(), v2CpuMaxPath(), v2CpuEffectivePath());
  CpuTimesV2 envoy_container_stats = container_stats_reader1.getCpuTimes();
  EXPECT_FALSE(envoy_container_stats.is_valid);
  auto result = container_stats_reader1.getUtilization();
  EXPECT_FALSE(result.ok());
  EXPECT_NE(result.status().message().find("Failed to read CPU times"), std::string::npos);

  // Test 2: Failed to parse - non-numeric quota
  setV2CpuMax("notanumber 100000\n");
  CgroupV2CpuStatsReader container_stats_reader2(
      api->fileSystem(), test_time_source, v2CpuStatPath(), v2CpuMaxPath(), v2CpuEffectivePath());
  envoy_container_stats = container_stats_reader2.getCpuTimes();
  EXPECT_FALSE(envoy_container_stats.is_valid);

  // Test 3: Invalid period value (zero)
  setV2CpuMax("200000 0\n");
  CgroupV2CpuStatsReader container_stats_reader3(
      api->fileSystem(), test_time_source, v2CpuStatPath(), v2CpuMaxPath(), v2CpuEffectivePath());
  envoy_container_stats = container_stats_reader3.getCpuTimes();
  EXPECT_FALSE(envoy_container_stats.is_valid);
}

// File read errors for V2
TEST_F(LinuxContainerCpuStatsReaderV2Test, CannotReadCpuStatFile) {
  TimeSource& test_time_source = timeSource();
  Api::ApiPtr api = Api::createApiForTest();

  const std::string nonexistent_stat = TestEnvironment::temporaryPath("nonexistent_cpu_stat");
  setV2CpuMax("200000 100000\n");
  setV2CpuEffective("0-3\n");

  CgroupV2CpuStatsReader container_stats_reader(
      api->fileSystem(), test_time_source, nonexistent_stat, v2CpuMaxPath(), v2CpuEffectivePath());
  CpuTimesV2 envoy_container_stats = container_stats_reader.getCpuTimes();

  EXPECT_FALSE(envoy_container_stats.is_valid);

  // Test that getUtilization also handles the error
  auto result = container_stats_reader.getUtilization();
  EXPECT_FALSE(result.ok());
  EXPECT_NE(result.status().message().find("Failed to read CPU times"), std::string::npos);
}

TEST_F(LinuxContainerCpuStatsReaderV2Test, CannotReadEffectiveCpusFile) {
  TimeSource& test_time_source = timeSource();
  Api::ApiPtr api = Api::createApiForTest();

  setV2CpuStat("usage_usec 500000\n");
  setV2CpuMax("200000 100000\n");
  const std::string nonexistent_effective = TestEnvironment::temporaryPath("nonexistent_effective");

  CgroupV2CpuStatsReader container_stats_reader(
      api->fileSystem(), test_time_source, v2CpuStatPath(), v2CpuMaxPath(), nonexistent_effective);
  CpuTimesV2 envoy_container_stats = container_stats_reader.getCpuTimes();

  EXPECT_FALSE(envoy_container_stats.is_valid);
}

TEST_F(LinuxContainerCpuStatsReaderV2Test, CannotReadCpuMaxFile) {
  TimeSource& test_time_source = timeSource();
  Api::ApiPtr api = Api::createApiForTest();

  setV2CpuStat("usage_usec 500000\n");
  setV2CpuEffective("0-3\n");
  const std::string nonexistent_max = TestEnvironment::temporaryPath("nonexistent_max");

  CgroupV2CpuStatsReader container_stats_reader(
      api->fileSystem(), test_time_source, v2CpuStatPath(), nonexistent_max, v2CpuEffectivePath());
  CpuTimesV2 envoy_container_stats = container_stats_reader.getCpuTimes();

  EXPECT_FALSE(envoy_container_stats.is_valid);
}

TEST_F(LinuxContainerCpuStatsReaderV2Test, V2GetUtilizationFirstCallReturnsZero) {
  TimeSource& test_time_source = timeSource();
  Api::ApiPtr api = Api::createApiForTest();
  setV2CpuStat("usage_usec 500000\n");
  setV2CpuMax("200000 100000\n");
  setV2CpuEffective("0-3\n");

  CgroupV2CpuStatsReader container_stats_reader(
      api->fileSystem(), test_time_source, v2CpuStatPath(), v2CpuMaxPath(), v2CpuEffectivePath());
  auto result = container_stats_reader.getUtilization();

  ASSERT_TRUE(result.ok());
  EXPECT_DOUBLE_EQ(result.value(), 0.0);

  // Also test negative work_over_period error scenario (usage decreased)
  setV2CpuStat("usage_usec 400000\n");
  result = container_stats_reader.getUtilization();
  EXPECT_FALSE(result.ok());
  EXPECT_NE(result.status().message().find("Work_over_period"), std::string::npos);
}

// =============================================================================
// Factory Method Tests
// =============================================================================

TEST(LinuxContainerCpuStatsReaderFactoryTest, CreatesV2ReaderWhenV2FilesExist) {
  Api::ApiPtr api = Api::createApiForTest();
  Event::MockDispatcher dispatcher;
  Server::MockOptions options;
  Server::Configuration::ResourceMonitorFactoryContextImpl context(
      dispatcher, options, *api, ProtobufMessage::getStrictValidationVisitor());

  Filesystem::MockInstance mock_fs;
  EXPECT_CALL(mock_fs, fileExists("/sys/fs/cgroup/cpu.stat")).WillOnce(Return(true));
  EXPECT_CALL(mock_fs, fileExists("/sys/fs/cgroup/cpu.max")).WillOnce(Return(true));
  EXPECT_CALL(mock_fs, fileExists("/sys/fs/cgroup/cpuset.cpus.effective")).WillOnce(Return(true));

  auto reader = LinuxContainerCpuStatsReader::create(mock_fs, context.api().timeSource());
  EXPECT_NE(reader, nullptr);
}

TEST(LinuxContainerCpuStatsReaderFactoryTest, CreatesV1ReaderWhenOnlyV1FilesExist) {
  Api::ApiPtr api = Api::createApiForTest();
  Event::MockDispatcher dispatcher;
  Server::MockOptions options;
  Server::Configuration::ResourceMonitorFactoryContextImpl context(
      dispatcher, options, *api, ProtobufMessage::getStrictValidationVisitor());

  Filesystem::MockInstance mock_fs;

  // V2 files don't exist
  EXPECT_CALL(mock_fs, fileExists("/sys/fs/cgroup/cpu.stat")).WillOnce(Return(false));
  EXPECT_CALL(mock_fs, fileExists("/sys/fs/cgroup/cpu.max"))
      .Times(testing::AtMost(1))
      .WillRepeatedly(Return(false));
  EXPECT_CALL(mock_fs, fileExists("/sys/fs/cgroup/cpuset.cpus.effective"))
      .Times(testing::AtMost(1))
      .WillRepeatedly(Return(false));

  // V1 files exist
  EXPECT_CALL(mock_fs, fileExists("/sys/fs/cgroup/cpu/cpu.shares")).WillOnce(Return(true));
  EXPECT_CALL(mock_fs, fileExists("/sys/fs/cgroup/cpuacct/cpuacct.usage")).WillOnce(Return(true));

  auto reader = LinuxContainerCpuStatsReader::create(mock_fs, context.api().timeSource());
  EXPECT_NE(reader, nullptr);
}

TEST(LinuxContainerCpuStatsReaderFactoryTest, ThrowsWhenNoCgroupFilesExist) {
  Api::ApiPtr api = Api::createApiForTest();
  Event::MockDispatcher dispatcher;
  Server::MockOptions options;
  Server::Configuration::ResourceMonitorFactoryContextImpl context(
      dispatcher, options, *api, ProtobufMessage::getStrictValidationVisitor());

  Filesystem::MockInstance mock_fs;

  // No V2 files
  EXPECT_CALL(mock_fs, fileExists("/sys/fs/cgroup/cpu.stat")).WillOnce(Return(false));
  EXPECT_CALL(mock_fs, fileExists("/sys/fs/cgroup/cpu.max"))
      .Times(testing::AtMost(1))
      .WillRepeatedly(Return(false));
  EXPECT_CALL(mock_fs, fileExists("/sys/fs/cgroup/cpuset.cpus.effective"))
      .Times(testing::AtMost(1))
      .WillRepeatedly(Return(false));

  // No V1 files
  EXPECT_CALL(mock_fs, fileExists("/sys/fs/cgroup/cpu/cpu.shares")).WillOnce(Return(false));
  EXPECT_CALL(mock_fs, fileExists("/sys/fs/cgroup/cpuacct/cpuacct.usage"))
      .Times(testing::AtMost(1))
      .WillRepeatedly(Return(false));

  EXPECT_THROW(LinuxContainerCpuStatsReader::create(mock_fs, context.api().timeSource()),
               EnvoyException);
}

// =============================================================================
// getUtilization() Method Tests
// =============================================================================

TEST(LinuxCpuStatsReaderUtilizationTest, FirstCallReturnsZero) {
  const std::string temp_path = TestEnvironment::temporaryPath("cpu_stats_util");
  AtomicFileUpdater file_updater(temp_path);
  file_updater.update("cpu  1000 100 200 500 0 0 0 0 0 0\n");

  LinuxCpuStatsReader cpu_stats_reader(temp_path);
  auto result = cpu_stats_reader.getUtilization();

  ASSERT_TRUE(result.ok());
  EXPECT_DOUBLE_EQ(result.value(), 0.0);
}

TEST(LinuxCpuStatsReaderUtilizationTest, CalculatesUtilizationCorrectly) {
  const std::string temp_path = TestEnvironment::temporaryPath("cpu_stats_util2");
  AtomicFileUpdater file_updater(temp_path);

  // First reading
  file_updater.update("cpu  1000 100 200 700 0 0 0 0 0 0\n");
  LinuxCpuStatsReader cpu_stats_reader(temp_path);
  auto result1 = cpu_stats_reader.getUtilization();
  ASSERT_TRUE(result1.ok());
  EXPECT_DOUBLE_EQ(result1.value(), 0.0); // First call returns 0

  // Second reading: work increased by 600, total increased by 1000
  file_updater.update("cpu  1600 100 200 1100 0 0 0 0 0 0\n");
  auto result2 = cpu_stats_reader.getUtilization();
  ASSERT_TRUE(result2.ok());
  EXPECT_DOUBLE_EQ(result2.value(), 0.6); // 600/1000
}

TEST(LinuxCpuStatsReaderUtilizationTest, InvalidFileReturnsError) {
  const std::string temp_path = TestEnvironment::temporaryPath("cpu_stats_not_exist_util");
  LinuxCpuStatsReader cpu_stats_reader(temp_path);
  auto result = cpu_stats_reader.getUtilization();

  EXPECT_FALSE(result.ok());
  EXPECT_NE(result.status().message().find("Failed to read CPU times"), std::string::npos);
}

TEST(LinuxCpuStatsReaderUtilizationTest, NegativeWorkDeltaReturnsError) {
  const std::string temp_path = TestEnvironment::temporaryPath("cpu_stats_util3");
  AtomicFileUpdater file_updater(temp_path);

  file_updater.update("cpu  1000 100 200 700 0 0 0 0 0 0\n");
  LinuxCpuStatsReader cpu_stats_reader(temp_path);
  (void)cpu_stats_reader.getUtilization(); // Initialize

  // Work decreased (clock regression)
  file_updater.update("cpu  500 100 200 1100 0 0 0 0 0 0\n");
  auto result = cpu_stats_reader.getUtilization();

  EXPECT_FALSE(result.ok());
  EXPECT_NE(result.status().message().find("Work_over_period"), std::string::npos);

  // Also test zero total_over_period by keeping stats unchanged
  file_updater.update("cpu  500 100 200 1100 0 0 0 0 0 0\n");
  result = cpu_stats_reader.getUtilization();

  EXPECT_FALSE(result.ok());
  EXPECT_NE(result.status().message().find("total_over_period"), std::string::npos);
}

} // namespace
} // namespace CpuUtilizationMonitor
} // namespace ResourceMonitors
} // namespace Extensions
} // namespace Envoy
