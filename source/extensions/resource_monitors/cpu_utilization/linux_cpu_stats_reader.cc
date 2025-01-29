#include "source/extensions/resource_monitors/cpu_utilization/linux_cpu_stats_reader.h"

#include <algorithm>
#include <chrono>
#include <vector>

#include "envoy/common/time.h"

namespace Envoy {
namespace Extensions {
namespace ResourceMonitors {
namespace CpuUtilizationMonitor {

constexpr uint64_t NUMBER_OF_CPU_TIMES_TO_PARSE =
    4; // we are interested in user, nice, system and idle times.
constexpr uint64_t CONTAINER_MILLICORES_PER_CORE = 1000;

LinuxCpuStatsReader::LinuxCpuStatsReader(const std::string& cpu_stats_filename)
    : cpu_stats_filename_(cpu_stats_filename) {}

CpuTimes LinuxCpuStatsReader::getCpuTimes() {
  std::ifstream cpu_stats_file;
  cpu_stats_file.open(cpu_stats_filename_);
  if (!cpu_stats_file.is_open()) {
    ENVOY_LOG_MISC(error, "Can't open linux cpu stats file {}", cpu_stats_filename_);
    return {false, 0, 0};
  }

  // The first 5 bytes should be 'cpu ' without a cpu index.
  std::string buffer(5, '\0');
  cpu_stats_file.read(buffer.data(), 5);
  const std::string target = "cpu  ";
  if (!cpu_stats_file || buffer != target) {
    ENVOY_LOG_MISC(error, "Unexpected format in linux cpu stats file {}", cpu_stats_filename_);
    return {false, 0, 0};
  }

  std::array<uint64_t, NUMBER_OF_CPU_TIMES_TO_PARSE> times;
  for (uint64_t time, i = 0; i < NUMBER_OF_CPU_TIMES_TO_PARSE; ++i) {
    cpu_stats_file >> time;
    if (!cpu_stats_file) {
      ENVOY_LOG_MISC(error, "Unexpected format in linux cpu stats file {}", cpu_stats_filename_);
      return {false, 0, 0};
    }
    times[i] = time;
  }

  uint64_t work_time, total_time;
  work_time = times[0] + times[1] + times[2]; // user + nice + system
  total_time = work_time + times[3];          // idle
  return {true, static_cast<double>(work_time), total_time};
}

LinuxContainerCpuStatsReader::LinuxContainerCpuStatsReader(
    TimeSource& time_source, const std::string& linux_cgroup_cpu_allocated_file,
    const std::string& linux_cgroup_cpu_times_file)
    : time_source_(time_source), linux_cgroup_cpu_allocated_file_(linux_cgroup_cpu_allocated_file),
      linux_cgroup_cpu_times_file_(linux_cgroup_cpu_times_file) {}

CpuTimes LinuxContainerCpuStatsReader::getCpuTimes() {
  std::ifstream cpu_allocated_file, cpu_times_file;
  double cpu_allocated_value, cpu_times_value;

  cpu_allocated_file.open(linux_cgroup_cpu_allocated_file_);
  if (!cpu_allocated_file.is_open()) {
    ENVOY_LOG_MISC(error, "Can't open linux cpu allocated file {}",
                   linux_cgroup_cpu_allocated_file_);
    return {false, 0, 0};
  }

  cpu_times_file.open(linux_cgroup_cpu_times_file_);
  if (!cpu_times_file.is_open()) {
    ENVOY_LOG_MISC(error, "Can't open linux cpu usage seconds file {}",
                   linux_cgroup_cpu_times_file_);
    return {false, 0, 0};
  }

  cpu_allocated_file >> cpu_allocated_value;
  if (!cpu_allocated_file) {
    ENVOY_LOG_MISC(error, "Unexpected format in linux cpu allocated file {}",
                   linux_cgroup_cpu_allocated_file_);
    return {false, 0, 0};
  }

  cpu_times_file >> cpu_times_value;
  if (!cpu_times_file) {
    ENVOY_LOG_MISC(error, "Unexpected format in linux cpu usage seconds file {}",
                   linux_cgroup_cpu_times_file_);
    return {false, 0, 0};
  }

  const uint64_t current_time = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                    time_source_.monotonicTime().time_since_epoch())
                                    .count();
  return {true, (cpu_times_value * CONTAINER_MILLICORES_PER_CORE) / cpu_allocated_value,
          current_time}; // cpu_times is in nanoseconds and cpu_allocated shares is in Millicores
}

} // namespace CpuUtilizationMonitor
} // namespace ResourceMonitors
} // namespace Extensions
} // namespace Envoy
