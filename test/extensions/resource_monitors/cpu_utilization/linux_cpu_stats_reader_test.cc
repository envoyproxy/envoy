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

  LinuxContainerCpuStatsReader container_stats_reader(test_time_source, cpuAllocatedPath(),
                                                      cpuTimesPath());
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

  LinuxContainerCpuStatsReader container_stats_reader(test_time_source, temp_path_cpu_allocated,
                                                      cpuTimesPath());
  CpuTimes envoy_container_stats = container_stats_reader.getCpuTimes();
  EXPECT_FALSE(envoy_container_stats.is_valid);
  EXPECT_EQ(envoy_container_stats.work_time, 0);
  EXPECT_EQ(envoy_container_stats.total_time, 0);
}

TEST_F(LinuxContainerCpuStatsReaderTest, CannotReadFileCpuTimes) {
  TimeSource& test_time_source = timeSource();
  const std::string temp_path_cpu_times =
      TestEnvironment::temporaryPath("container_cpu_times_not_exists");

  LinuxContainerCpuStatsReader container_stats_reader(test_time_source, cpuAllocatedPath(),
                                                      temp_path_cpu_times);
  CpuTimes envoy_container_stats = container_stats_reader.getCpuTimes();
  EXPECT_FALSE(envoy_container_stats.is_valid);
  EXPECT_EQ(envoy_container_stats.work_time, 0);
  EXPECT_EQ(envoy_container_stats.total_time, 0);
}

TEST_F(LinuxContainerCpuStatsReaderTest, UnexpectedFormatCpuAllocatedLine) {
  TimeSource& test_time_source = timeSource();
  setCpuAllocated("notanumb3r\n");

  LinuxContainerCpuStatsReader container_stats_reader(test_time_source, cpuAllocatedPath(),
                                                      cpuTimesPath());
  CpuTimes envoy_container_stats = container_stats_reader.getCpuTimes();
  EXPECT_FALSE(envoy_container_stats.is_valid);
  EXPECT_EQ(envoy_container_stats.work_time, 0);
  EXPECT_EQ(envoy_container_stats.total_time, 0);
}

TEST_F(LinuxContainerCpuStatsReaderTest, UnexpectedFormatCpuTimesLine) {
  TimeSource& test_time_source = timeSource();
  setCpuTimes("notanumb3r\n");

  LinuxContainerCpuStatsReader container_stats_reader(test_time_source, cpuAllocatedPath(),
                                                      cpuTimesPath());
  CpuTimes envoy_container_stats = container_stats_reader.getCpuTimes();
  EXPECT_FALSE(envoy_container_stats.is_valid);
  EXPECT_EQ(envoy_container_stats.work_time, 0);
  EXPECT_EQ(envoy_container_stats.total_time, 0);
}

} // namespace
} // namespace CpuUtilizationMonitor
} // namespace ResourceMonitors
} // namespace Extensions
} // namespace Envoy
