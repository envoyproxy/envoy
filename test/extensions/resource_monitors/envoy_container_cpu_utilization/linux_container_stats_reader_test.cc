#include <cstdlib>
#include <chrono>
#include <iostream>
#include "source/extensions/resource_monitors/envoy_container_cpu_utilization/container_stats_reader.h"
#include "source/extensions/resource_monitors/envoy_container_cpu_utilization/linux_container_stats_reader.h"

#include "test/test_common/environment.h"

#include "absl/types/optional.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace ResourceMonitors {
namespace EnvoyContainerCpuUtilizationMonitor {
namespace {

TEST(LinuxContainerStatsReader, ReadsCgroupContainerStats) {
  const std::string temp_path_cpu_allocated = TestEnvironment::temporaryPath("cgroup_cpu_allocated_stats");
  AtomicFileUpdater file_updater_cpu_allocated(temp_path_cpu_allocated);
  const std::string mock_contents_cpu_allocated = R"EOF(1000
)EOF";
  file_updater_cpu_allocated.update(mock_contents_cpu_allocated);

  const std::string temp_path_cpu_times = TestEnvironment::temporaryPath("cgroup_cpu_times_stats");
  AtomicFileUpdater file_updater_cpu_times(temp_path_cpu_times);
  const std::string mock_contents_cpu_times = R"EOF(10000000000
)EOF";

  file_updater_cpu_times.update(mock_contents_cpu_times);
  LinuxContainerStatsReader container_stats_reader(temp_path_cpu_allocated,temp_path_cpu_times);
  EnvoyContainerStats envoy_container_stats = container_stats_reader.getEnvoyContainerStats();
  EXPECT_EQ(envoy_container_stats.cpu_allocated_millicores_,1000);
  EXPECT_EQ(envoy_container_stats.total_cpu_times_ns_, 10000000000);
  EXPECT_EQ(std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now() - envoy_container_stats.system_time_ ).count(),0);
}

TEST(LinuxContainerStatsReader, CannotReadFile) {
  const std::string temp_path_cpu_allocated = TestEnvironment::temporaryPath("container_cpu_allocated_not_exists");
  const std::string temp_path_cpu_times = TestEnvironment::temporaryPath("container_cpu_times_not_exists");
  LinuxContainerStatsReader container_stats_reader(temp_path_cpu_allocated,temp_path_cpu_times);
  EnvoyContainerStats envoy_container_stats = container_stats_reader.getEnvoyContainerStats();
  EXPECT_FALSE(envoy_container_stats.is_valid);
  EXPECT_EQ(envoy_container_stats.cpu_allocated_millicores_, 0);
  EXPECT_EQ(envoy_container_stats.total_cpu_times_ns_, 0);
  EXPECT_EQ(std::chrono::duration_cast<std::chrono::seconds>( std::chrono::system_clock::now() - envoy_container_stats.system_time_ ).count(),0);
}

TEST(LinuxContainerStatsReader, UnexpectedFormatCpuAllocatedLine) {
  const std::string temp_path = TestEnvironment::temporaryPath("container_cpu_allocated_unexpected_format");
  AtomicFileUpdater file_updater(temp_path);
  const std::string contents = R"EOF(notanumb3r
)EOF";
  file_updater.update(contents);

  LinuxContainerStatsReader container_stats_reader(temp_path);
  EnvoyContainerStats envoy_container_stats = container_stats_reader.getEnvoyContainerStats();
  EXPECT_FALSE(envoy_container_stats.is_valid);
  EXPECT_EQ(envoy_container_stats.cpu_allocated_millicores_, 0);
  EXPECT_EQ(envoy_container_stats.total_cpu_times_ns_, 0);
  EXPECT_EQ(std::chrono::duration_cast<std::chrono::seconds>( std::chrono::system_clock::now() - envoy_container_stats.system_time_ ).count(),0);
}

TEST(LinuxContainerStatsReader, UnexpectedFormatCpuTimesLine) {
  const std::string temp_path = TestEnvironment::temporaryPath("container_cpu_times_unexpected_format");
  AtomicFileUpdater file_updater(temp_path);
  const std::string contents = R"EOF(not@number
)EOF";
  file_updater.update(contents);

  LinuxContainerStatsReader container_stats_reader(temp_path);
  EnvoyContainerStats envoy_container_stats = container_stats_reader.getEnvoyContainerStats();
  EXPECT_FALSE(envoy_container_stats.is_valid);
  EXPECT_EQ(envoy_container_stats.cpu_allocated_millicores_, 0);
  EXPECT_EQ(envoy_container_stats.total_cpu_times_ns_, 0);
  EXPECT_EQ(std::chrono::duration_cast<std::chrono::seconds>( std::chrono::system_clock::now() - envoy_container_stats.system_time_ ).count(),0);
}



} // namespace
} // namespace EnvoyContainerCpuUtilizationMonitor
} // namespace ResourceMonitors
} // namespace Extensions
} // namespace Envoy
