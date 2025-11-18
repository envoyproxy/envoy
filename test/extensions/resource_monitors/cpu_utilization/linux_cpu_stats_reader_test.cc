#include <chrono>
#include <cstdlib>

#include "source/extensions/resource_monitors/cpu_utilization/cpu_stats_reader.h"
#include "source/extensions/resource_monitors/cpu_utilization/linux_cpu_stats_reader.h"
#include "source/server/resource_monitor_config_impl.h"

#include "test/mocks/event/mocks.h"
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
  CpuTimes cpu_times = cpu_stats_reader.getCpuTimes();

  EXPECT_EQ(cpu_times.work_time, 17995597);
  EXPECT_EQ(cpu_times.total_time, 29590585);
}

TEST(LinuxCpuStatsReader, CannotReadFile) {
  const std::string temp_path = TestEnvironment::temporaryPath("cpu_stats_not_exists");
  LinuxCpuStatsReader cpu_stats_reader(temp_path);
  CpuTimes cpu_times = cpu_stats_reader.getCpuTimes();
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
  CpuTimes cpu_times = cpu_stats_reader.getCpuTimes();
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
  CpuTimes cpu_times = cpu_stats_reader.getCpuTimes();
  EXPECT_FALSE(cpu_times.is_valid);
  EXPECT_EQ(cpu_times.work_time, 0);
  EXPECT_EQ(cpu_times.total_time, 0);
}

class LinuxContainerCpuStatsReaderTest : public testing::Test {
public:
  LinuxContainerCpuStatsReaderTest()
      : api_(Api::createApiForTest()),
        context_(dispatcher_, options_, *api_, ProtobufMessage::getStrictValidationVisitor()),
        cpu_allocated_path_(TestEnvironment::temporaryPath("cgroup_cpu_allocated_stats")),
        cpu_times_path_(TestEnvironment::temporaryPath("cgroup_cpu_times_stats")) {
    // We populate the files that LinuxContainerStatsReader tries to read with some default
    // sane values, so the tests don't need to populate the files they don't actually care
    // about keeping the test cases focused on what they actually want to test.
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
  setCpuAllocated("2000\n");
  setCpuTimes("1000\n");

  // Pass non-existent paths for cgroup v2 files to test cgroup v1 behavior
  const std::string nonexistent_path = TestEnvironment::temporaryPath("nonexistent");
  LinuxContainerCpuStatsReader container_stats_reader(test_time_source, cpuAllocatedPath(),
                                                      cpuTimesPath(), nonexistent_path,
                                                      nonexistent_path, nonexistent_path);
  CpuTimes envoy_container_stats = container_stats_reader.getCpuTimes();

  const uint64_t current_monotonic_time = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                              test_time_source.monotonicTime().time_since_epoch())
                                              .count();
  const int64_t time_diff_ns = current_monotonic_time - envoy_container_stats.total_time;

  EXPECT_EQ(envoy_container_stats.work_time, 500);
  EXPECT_GT(time_diff_ns, 0);
}

TEST_F(LinuxContainerCpuStatsReaderTest, CannotReadFileCpuAllocated) {
  TimeSource& test_time_source = timeSource();
  const std::string temp_path_cpu_allocated =
      TestEnvironment::temporaryPath("container_cpu_times_not_exists");

  // Pass non-existent paths for cgroup v2 files to test cgroup v1 behavior
  const std::string nonexistent_path = TestEnvironment::temporaryPath("nonexistent");
  LinuxContainerCpuStatsReader container_stats_reader(test_time_source, temp_path_cpu_allocated,
                                                      cpuTimesPath(), nonexistent_path,
                                                      nonexistent_path, nonexistent_path);
  CpuTimes envoy_container_stats = container_stats_reader.getCpuTimes();
  EXPECT_FALSE(envoy_container_stats.is_valid);
  EXPECT_EQ(envoy_container_stats.work_time, 0);
  EXPECT_EQ(envoy_container_stats.total_time, 0);
}

TEST_F(LinuxContainerCpuStatsReaderTest, CannotReadFileCpuTimes) {
  TimeSource& test_time_source = timeSource();
  const std::string temp_path_cpu_times =
      TestEnvironment::temporaryPath("container_cpu_times_not_exists");

  // Pass non-existent paths for cgroup v2 files to test cgroup v1 behavior
  const std::string nonexistent_path = TestEnvironment::temporaryPath("nonexistent");
  LinuxContainerCpuStatsReader container_stats_reader(test_time_source, cpuAllocatedPath(),
                                                      temp_path_cpu_times, nonexistent_path,
                                                      nonexistent_path, nonexistent_path);
  CpuTimes envoy_container_stats = container_stats_reader.getCpuTimes();
  EXPECT_FALSE(envoy_container_stats.is_valid);
  EXPECT_EQ(envoy_container_stats.work_time, 0);
  EXPECT_EQ(envoy_container_stats.total_time, 0);
}

TEST_F(LinuxContainerCpuStatsReaderTest, UnexpectedFormatCpuAllocatedLine) {
  TimeSource& test_time_source = timeSource();
  setCpuAllocated("notanumb3r\n");

  // Pass non-existent paths for cgroup v2 files to test cgroup v1 behavior
  const std::string nonexistent_path = TestEnvironment::temporaryPath("nonexistent");
  LinuxContainerCpuStatsReader container_stats_reader(test_time_source, cpuAllocatedPath(),
                                                      cpuTimesPath(), nonexistent_path,
                                                      nonexistent_path, nonexistent_path);
  CpuTimes envoy_container_stats = container_stats_reader.getCpuTimes();
  EXPECT_FALSE(envoy_container_stats.is_valid);
  EXPECT_EQ(envoy_container_stats.work_time, 0);
  EXPECT_EQ(envoy_container_stats.total_time, 0);
}

TEST_F(LinuxContainerCpuStatsReaderTest, UnexpectedFormatCpuTimesLine) {
  TimeSource& test_time_source = timeSource();
  setCpuTimes("notanumb3r\n");

  // Pass non-existent paths for cgroup v2 files to test cgroup v1 behavior
  const std::string nonexistent_path = TestEnvironment::temporaryPath("nonexistent");
  LinuxContainerCpuStatsReader container_stats_reader(test_time_source, cpuAllocatedPath(),
                                                      cpuTimesPath(), nonexistent_path,
                                                      nonexistent_path, nonexistent_path);
  CpuTimes envoy_container_stats = container_stats_reader.getCpuTimes();
  EXPECT_FALSE(envoy_container_stats.is_valid);
  EXPECT_EQ(envoy_container_stats.work_time, 0);
  EXPECT_EQ(envoy_container_stats.total_time, 0);
}

// Test fixture for cgroup v2 tests
class LinuxContainerCpuStatsReaderV2Test : public testing::Test {
public:
  LinuxContainerCpuStatsReaderV2Test()
      : api_(Api::createApiForTest()),
        context_(dispatcher_, options_, *api_, ProtobufMessage::getStrictValidationVisitor()),
        v2_cpu_stat_path_(TestEnvironment::temporaryPath("cgroupv2_cpu_stat")),
        v2_cpu_max_path_(TestEnvironment::temporaryPath("cgroupv2_cpu_max")),
        v2_cpu_effective_path_(TestEnvironment::temporaryPath("cgroupv2_cpu_effective")),
        v1_cpu_allocated_path_(TestEnvironment::temporaryPath("cgroupv1_cpu_allocated")),
        v1_cpu_times_path_(TestEnvironment::temporaryPath("cgroupv1_cpu_times")) {}

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

  const std::string& v1CpuAllocatedPath() const { return v1_cpu_allocated_path_; }
  const std::string& v1CpuTimesPath() const { return v1_cpu_times_path_; }

private:
  Event::MockDispatcher dispatcher_;
  Api::ApiPtr api_;
  Server::MockOptions options_;
  Server::Configuration::ResourceMonitorFactoryContextImpl context_;
  std::string v2_cpu_stat_path_;
  std::string v2_cpu_max_path_;
  std::string v2_cpu_effective_path_;
  std::string v1_cpu_allocated_path_;
  std::string v1_cpu_times_path_;
};

// Test: Happy path - all v2 files properly formatted with CPU range
TEST_F(LinuxContainerCpuStatsReaderV2Test, ReadsCgroupV2StatsWithCpuRange) {
  TimeSource& test_time_source = timeSource();
  // Set up cgroup v2 files
  setV2CpuStat("usage_usec 500000\nuser_usec 300000\nsystem_usec 200000\n");
  setV2CpuMax("200000 100000\n"); // quota=200000, period=100000 => 2.0 cores
  setV2CpuEffective("0-3\n");     // 4 cores available

  LinuxContainerCpuStatsReader container_stats_reader(test_time_source, v1CpuAllocatedPath(),
                                                      v1CpuTimesPath(), v2CpuStatPath(),
                                                      v2CpuMaxPath(), v2CpuEffectivePath());
  CpuTimes envoy_container_stats = container_stats_reader.getCpuTimes();

  EXPECT_TRUE(envoy_container_stats.is_valid);
  EXPECT_TRUE(envoy_container_stats.is_cgroup_v2);
  EXPECT_EQ(envoy_container_stats.work_time, 500000); // usage_usec
  EXPECT_GT(envoy_container_stats.total_time, 0);
  EXPECT_DOUBLE_EQ(envoy_container_stats.effective_cores, 2.0); // min(4, 200000/100000)
}

// Test: Happy path - all v2 files with single CPU
TEST_F(LinuxContainerCpuStatsReaderV2Test, ReadsCgroupV2StatsWithSingleCpu) {
  TimeSource& test_time_source = timeSource();
  setV2CpuStat("usage_usec 750000\n");
  setV2CpuMax("50000 100000\n"); // quota=50000, period=100000 => 0.5 cores
  setV2CpuEffective("0\n");      // 1 core available

  LinuxContainerCpuStatsReader container_stats_reader(test_time_source, v1CpuAllocatedPath(),
                                                      v1CpuTimesPath(), v2CpuStatPath(),
                                                      v2CpuMaxPath(), v2CpuEffectivePath());
  CpuTimes envoy_container_stats = container_stats_reader.getCpuTimes();

  EXPECT_TRUE(envoy_container_stats.is_valid);
  EXPECT_TRUE(envoy_container_stats.is_cgroup_v2);
  EXPECT_EQ(envoy_container_stats.work_time, 750000);
  EXPECT_DOUBLE_EQ(envoy_container_stats.effective_cores, 0.5); // min(1, 0.5)
}

// Test: "max" quota means no CPU limit
TEST_F(LinuxContainerCpuStatsReaderV2Test, ReadsCgroupV2StatsWithMaxQuota) {
  TimeSource& test_time_source = timeSource();
  setV2CpuStat("usage_usec 1000000\n");
  setV2CpuMax("max 100000\n"); // No CPU limit
  setV2CpuEffective("0-7\n");  // 8 cores available

  LinuxContainerCpuStatsReader container_stats_reader(test_time_source, v1CpuAllocatedPath(),
                                                      v1CpuTimesPath(), v2CpuStatPath(),
                                                      v2CpuMaxPath(), v2CpuEffectivePath());
  CpuTimes envoy_container_stats = container_stats_reader.getCpuTimes();

  EXPECT_TRUE(envoy_container_stats.is_valid);
  EXPECT_TRUE(envoy_container_stats.is_cgroup_v2);
  EXPECT_EQ(envoy_container_stats.work_time, 1000000);
  EXPECT_DOUBLE_EQ(envoy_container_stats.effective_cores, 8.0); // No quota limit, use N
}

// Test: Large CPU range
TEST_F(LinuxContainerCpuStatsReaderV2Test, ReadsCgroupV2StatsWithLargeCpuRange) {
  TimeSource& test_time_source = timeSource();
  setV2CpuStat("usage_usec 2000000\n");
  setV2CpuMax("1600000 100000\n"); // quota=1600000, period=100000 => 16.0 cores
  setV2CpuEffective("0-31\n");     // 32 cores available

  LinuxContainerCpuStatsReader container_stats_reader(test_time_source, v1CpuAllocatedPath(),
                                                      v1CpuTimesPath(), v2CpuStatPath(),
                                                      v2CpuMaxPath(), v2CpuEffectivePath());
  CpuTimes envoy_container_stats = container_stats_reader.getCpuTimes();

  EXPECT_TRUE(envoy_container_stats.is_valid);
  EXPECT_TRUE(envoy_container_stats.is_cgroup_v2);
  EXPECT_DOUBLE_EQ(envoy_container_stats.effective_cores, 16.0); // min(32, 16)
}

// Test: Missing usage_usec in cpu.stat
TEST_F(LinuxContainerCpuStatsReaderV2Test, MissingUsageUsecInCpuStat) {
  TimeSource& test_time_source = timeSource();
  setV2CpuStat("user_usec 300000\nsystem_usec 200000\n"); // No usage_usec
  setV2CpuMax("200000 100000\n");
  setV2CpuEffective("0-3\n");

  LinuxContainerCpuStatsReader container_stats_reader(test_time_source, v1CpuAllocatedPath(),
                                                      v1CpuTimesPath(), v2CpuStatPath(),
                                                      v2CpuMaxPath(), v2CpuEffectivePath());
  CpuTimes envoy_container_stats = container_stats_reader.getCpuTimes();

  EXPECT_FALSE(envoy_container_stats.is_valid);
  EXPECT_TRUE(envoy_container_stats.is_cgroup_v2);
}

// Test: Invalid numeric format in usage_usec
TEST_F(LinuxContainerCpuStatsReaderV2Test, InvalidUsageUsecFormat) {
  TimeSource& test_time_source = timeSource();
  setV2CpuStat("usage_usec notanumber\n");
  setV2CpuMax("200000 100000\n");
  setV2CpuEffective("0-3\n");

  LinuxContainerCpuStatsReader container_stats_reader(test_time_source, v1CpuAllocatedPath(),
                                                      v1CpuTimesPath(), v2CpuStatPath(),
                                                      v2CpuMaxPath(), v2CpuEffectivePath());
  CpuTimes envoy_container_stats = container_stats_reader.getCpuTimes();

  EXPECT_FALSE(envoy_container_stats.is_valid);
  EXPECT_TRUE(envoy_container_stats.is_cgroup_v2);
}

// Test: Invalid CPU value (negative) in cpuset.cpus.effective
TEST_F(LinuxContainerCpuStatsReaderV2Test, InvalidNegativeCpuValue) {
  TimeSource& test_time_source = timeSource();
  setV2CpuStat("usage_usec 500000\n");
  setV2CpuMax("200000 100000\n");
  setV2CpuEffective("-1\n"); // Invalid negative CPU

  LinuxContainerCpuStatsReader container_stats_reader(test_time_source, v1CpuAllocatedPath(),
                                                      v1CpuTimesPath(), v2CpuStatPath(),
                                                      v2CpuMaxPath(), v2CpuEffectivePath());
  CpuTimes envoy_container_stats = container_stats_reader.getCpuTimes();

  EXPECT_FALSE(envoy_container_stats.is_valid);
  EXPECT_TRUE(envoy_container_stats.is_cgroup_v2);
}

// Test: Invalid CPU range (start > end) in cpuset.cpus.effective
TEST_F(LinuxContainerCpuStatsReaderV2Test, InvalidCpuRangeStartGreaterThanEnd) {
  TimeSource& test_time_source = timeSource();
  setV2CpuStat("usage_usec 500000\n");
  setV2CpuMax("200000 100000\n");
  setV2CpuEffective("5-2\n"); // Invalid range: start > end

  LinuxContainerCpuStatsReader container_stats_reader(test_time_source, v1CpuAllocatedPath(),
                                                      v1CpuTimesPath(), v2CpuStatPath(),
                                                      v2CpuMaxPath(), v2CpuEffectivePath());
  CpuTimes envoy_container_stats = container_stats_reader.getCpuTimes();

  EXPECT_FALSE(envoy_container_stats.is_valid);
  EXPECT_TRUE(envoy_container_stats.is_cgroup_v2);
}

// Test: Invalid CPU range (negative start) in cpuset.cpus.effective
TEST_F(LinuxContainerCpuStatsReaderV2Test, InvalidCpuRangeNegativeStart) {
  TimeSource& test_time_source = timeSource();
  setV2CpuStat("usage_usec 500000\n");
  setV2CpuMax("200000 100000\n");
  setV2CpuEffective("-1-3\n"); // Invalid range: negative start

  LinuxContainerCpuStatsReader container_stats_reader(test_time_source, v1CpuAllocatedPath(),
                                                      v1CpuTimesPath(), v2CpuStatPath(),
                                                      v2CpuMaxPath(), v2CpuEffectivePath());
  CpuTimes envoy_container_stats = container_stats_reader.getCpuTimes();

  EXPECT_FALSE(envoy_container_stats.is_valid);
  EXPECT_TRUE(envoy_container_stats.is_cgroup_v2);
}

// Test: Invalid numeric format in cpuset.cpus.effective
TEST_F(LinuxContainerCpuStatsReaderV2Test, InvalidCpuEffectiveFormat) {
  TimeSource& test_time_source = timeSource();
  setV2CpuStat("usage_usec 500000\n");
  setV2CpuMax("200000 100000\n");
  setV2CpuEffective("notanumber\n");

  LinuxContainerCpuStatsReader container_stats_reader(test_time_source, v1CpuAllocatedPath(),
                                                      v1CpuTimesPath(), v2CpuStatPath(),
                                                      v2CpuMaxPath(), v2CpuEffectivePath());
  CpuTimes envoy_container_stats = container_stats_reader.getCpuTimes();

  EXPECT_FALSE(envoy_container_stats.is_valid);
  EXPECT_TRUE(envoy_container_stats.is_cgroup_v2);
}

// Test: Empty cpuset.cpus.effective file
TEST_F(LinuxContainerCpuStatsReaderV2Test, EmptyCpuEffectiveFile) {
  TimeSource& test_time_source = timeSource();
  setV2CpuStat("usage_usec 500000\n");
  setV2CpuMax("200000 100000\n");
  setV2CpuEffective(""); // Empty file

  LinuxContainerCpuStatsReader container_stats_reader(test_time_source, v1CpuAllocatedPath(),
                                                      v1CpuTimesPath(), v2CpuStatPath(),
                                                      v2CpuMaxPath(), v2CpuEffectivePath());
  CpuTimes envoy_container_stats = container_stats_reader.getCpuTimes();

  EXPECT_FALSE(envoy_container_stats.is_valid);
  EXPECT_TRUE(envoy_container_stats.is_cgroup_v2);
}

// Test: Invalid period value (zero) in cpu.max
TEST_F(LinuxContainerCpuStatsReaderV2Test, InvalidZeroPeriodInCpuMax) {
  TimeSource& test_time_source = timeSource();
  setV2CpuStat("usage_usec 500000\n");
  setV2CpuMax("200000 0\n"); // Invalid period: 0
  setV2CpuEffective("0-3\n");

  LinuxContainerCpuStatsReader container_stats_reader(test_time_source, v1CpuAllocatedPath(),
                                                      v1CpuTimesPath(), v2CpuStatPath(),
                                                      v2CpuMaxPath(), v2CpuEffectivePath());
  CpuTimes envoy_container_stats = container_stats_reader.getCpuTimes();

  EXPECT_FALSE(envoy_container_stats.is_valid);
  EXPECT_TRUE(envoy_container_stats.is_cgroup_v2);
}

// Test: Invalid period value (negative) in cpu.max
TEST_F(LinuxContainerCpuStatsReaderV2Test, InvalidNegativePeriodInCpuMax) {
  TimeSource& test_time_source = timeSource();
  setV2CpuStat("usage_usec 500000\n");
  setV2CpuMax("200000 -100\n"); // Invalid period: negative
  setV2CpuEffective("0-3\n");

  LinuxContainerCpuStatsReader container_stats_reader(test_time_source, v1CpuAllocatedPath(),
                                                      v1CpuTimesPath(), v2CpuStatPath(),
                                                      v2CpuMaxPath(), v2CpuEffectivePath());
  CpuTimes envoy_container_stats = container_stats_reader.getCpuTimes();

  EXPECT_FALSE(envoy_container_stats.is_valid);
  EXPECT_TRUE(envoy_container_stats.is_cgroup_v2);
}

// Test: Invalid numeric format in cpu.max quota
TEST_F(LinuxContainerCpuStatsReaderV2Test, InvalidQuotaFormatInCpuMax) {
  TimeSource& test_time_source = timeSource();
  setV2CpuStat("usage_usec 500000\n");
  setV2CpuMax("notanumber 100000\n");
  setV2CpuEffective("0-3\n");

  LinuxContainerCpuStatsReader container_stats_reader(test_time_source, v1CpuAllocatedPath(),
                                                      v1CpuTimesPath(), v2CpuStatPath(),
                                                      v2CpuMaxPath(), v2CpuEffectivePath());
  CpuTimes envoy_container_stats = container_stats_reader.getCpuTimes();

  EXPECT_FALSE(envoy_container_stats.is_valid);
  EXPECT_TRUE(envoy_container_stats.is_cgroup_v2);
}

// Test: Invalid numeric format in cpu.max period
TEST_F(LinuxContainerCpuStatsReaderV2Test, InvalidPeriodFormatInCpuMax) {
  TimeSource& test_time_source = timeSource();
  setV2CpuStat("usage_usec 500000\n");
  setV2CpuMax("200000 notanumber\n");
  setV2CpuEffective("0-3\n");

  LinuxContainerCpuStatsReader container_stats_reader(test_time_source, v1CpuAllocatedPath(),
                                                      v1CpuTimesPath(), v2CpuStatPath(),
                                                      v2CpuMaxPath(), v2CpuEffectivePath());
  CpuTimes envoy_container_stats = container_stats_reader.getCpuTimes();

  EXPECT_FALSE(envoy_container_stats.is_valid);
  EXPECT_TRUE(envoy_container_stats.is_cgroup_v2);
}

// Test: Missing period in cpu.max (only quota provided)
TEST_F(LinuxContainerCpuStatsReaderV2Test, MissingPeriodInCpuMax) {
  TimeSource& test_time_source = timeSource();
  setV2CpuStat("usage_usec 500000\n");
  setV2CpuMax("200000\n"); // Missing period
  setV2CpuEffective("0-3\n");

  LinuxContainerCpuStatsReader container_stats_reader(test_time_source, v1CpuAllocatedPath(),
                                                      v1CpuTimesPath(), v2CpuStatPath(),
                                                      v2CpuMaxPath(), v2CpuEffectivePath());
  CpuTimes envoy_container_stats = container_stats_reader.getCpuTimes();

  EXPECT_FALSE(envoy_container_stats.is_valid);
  EXPECT_TRUE(envoy_container_stats.is_cgroup_v2);
}

// Test: Empty cpu.max file
TEST_F(LinuxContainerCpuStatsReaderV2Test, EmptyCpuMaxFile) {
  TimeSource& test_time_source = timeSource();
  setV2CpuStat("usage_usec 500000\n");
  setV2CpuMax(""); // Empty file
  setV2CpuEffective("0-3\n");

  LinuxContainerCpuStatsReader container_stats_reader(test_time_source, v1CpuAllocatedPath(),
                                                      v1CpuTimesPath(), v2CpuStatPath(),
                                                      v2CpuMaxPath(), v2CpuEffectivePath());
  CpuTimes envoy_container_stats = container_stats_reader.getCpuTimes();

  EXPECT_FALSE(envoy_container_stats.is_valid);
  EXPECT_TRUE(envoy_container_stats.is_cgroup_v2);
}

// Test: Empty cpu.stat file
TEST_F(LinuxContainerCpuStatsReaderV2Test, EmptyCpuStatFile) {
  TimeSource& test_time_source = timeSource();
  setV2CpuStat(""); // Empty file
  setV2CpuMax("200000 100000\n");
  setV2CpuEffective("0-3\n");

  LinuxContainerCpuStatsReader container_stats_reader(test_time_source, v1CpuAllocatedPath(),
                                                      v1CpuTimesPath(), v2CpuStatPath(),
                                                      v2CpuMaxPath(), v2CpuEffectivePath());
  CpuTimes envoy_container_stats = container_stats_reader.getCpuTimes();

  EXPECT_FALSE(envoy_container_stats.is_valid);
  EXPECT_TRUE(envoy_container_stats.is_cgroup_v2);
}

// Test: Quota exceeds available cores
TEST_F(LinuxContainerCpuStatsReaderV2Test, QuotaExceedsAvailableCores) {
  TimeSource& test_time_source = timeSource();
  setV2CpuStat("usage_usec 500000\n");
  setV2CpuMax("800000 100000\n"); // quota=800000, period=100000 => 8.0 cores
  setV2CpuEffective("0-3\n");     // Only 4 cores available

  LinuxContainerCpuStatsReader container_stats_reader(test_time_source, v1CpuAllocatedPath(),
                                                      v1CpuTimesPath(), v2CpuStatPath(),
                                                      v2CpuMaxPath(), v2CpuEffectivePath());
  CpuTimes envoy_container_stats = container_stats_reader.getCpuTimes();

  EXPECT_TRUE(envoy_container_stats.is_valid);
  EXPECT_TRUE(envoy_container_stats.is_cgroup_v2);
  EXPECT_DOUBLE_EQ(envoy_container_stats.effective_cores,
                   4.0); // min(4, 8) = 4, limited by available cores
}

} // namespace
} // namespace CpuUtilizationMonitor
} // namespace ResourceMonitors
} // namespace Extensions
} // namespace Envoy
