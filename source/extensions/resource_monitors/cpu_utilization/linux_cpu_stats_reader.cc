#include "source/extensions/resource_monitors/cpu_utilization/linux_cpu_stats_reader.h"

#include <algorithm>
#include <chrono>
#include <vector>

#include "envoy/common/exception.h"
#include "envoy/common/time.h"

#include "source/common/common/assert.h"
#include "source/common/common/thread.h"

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
    return {false, false, 0, 0, 0};
  }

  // The first 5 bytes should be 'cpu ' without a cpu index.
  std::string buffer(5, '\0');
  cpu_stats_file.read(buffer.data(), 5);
  const std::string target = "cpu  ";
  if (!cpu_stats_file || buffer != target) {
    ENVOY_LOG_MISC(error, "Unexpected format in linux cpu stats file {}", cpu_stats_filename_);
    return {false, false, 0, 0, 0};
  }

  std::array<uint64_t, NUMBER_OF_CPU_TIMES_TO_PARSE> times;
  for (uint64_t time, i = 0; i < NUMBER_OF_CPU_TIMES_TO_PARSE; ++i) {
    cpu_stats_file >> time;
    if (!cpu_stats_file) {
      ENVOY_LOG_MISC(error, "Unexpected format in linux cpu stats file {}", cpu_stats_filename_);
      return {false, false, 0, 0, 0};
    }
    times[i] = time;
  }

  uint64_t work_time, total_time;
  work_time = times[0] + times[1] + times[2]; // user + nice + system
  total_time = work_time + times[3];          // idle
  return {true, false, static_cast<double>(work_time), total_time, 0};
}

LinuxContainerCpuStatsReader::LinuxContainerCpuStatsReader(
    TimeSource& time_source, const std::string& linux_cgroup_cpu_allocated_file,
    const std::string& linux_cgroup_cpu_times_file, const std::string& cgroupv2_cpu_stat_file,
    const std::string& cgroupv2_cpu_max_file, const std::string& cgroupv2_cpu_effective_file)
    : time_source_(time_source), linux_cgroup_cpu_allocated_file_(linux_cgroup_cpu_allocated_file),
      linux_cgroup_cpu_times_file_(linux_cgroup_cpu_times_file),
      cgroupv2_cpu_stat_file_(cgroupv2_cpu_stat_file),
      cgroupv2_cpu_max_file_(cgroupv2_cpu_max_file),
      cgroupv2_cpu_effective_file_(cgroupv2_cpu_effective_file) {}

CpuTimes LinuxContainerCpuStatsReader::getCpuTimes() {

  // Prefer cgroup v2 files if they exist, else fall back to cgroup v1
  std::ifstream v2_stat_file(cgroupv2_cpu_stat_file_);
  std::ifstream v2_max_file(cgroupv2_cpu_max_file_);
  std::ifstream v2_effective_file(cgroupv2_cpu_effective_file_);

  if (v2_stat_file.is_open() && v2_max_file.is_open() && v2_effective_file.is_open()) {
    // Parse usage_usec from cpu.stat
    std::string line;
    uint64_t usage_usec = 0;
    bool found_usage = false;
    while (std::getline(v2_stat_file, line)) {
      if (line.rfind("usage_usec ", 0) == 0) {
        // line starts with "usage_usec "
        // expected format: usage_usec <value>
        const size_t pos = line.find_last_of(' ');
        if (pos != std::string::npos) {
          TRY_ASSERT_MAIN_THREAD {
            usage_usec = static_cast<uint64_t>(std::stoull(line.substr(pos + 1)));
            found_usage = true;
          }
          END_TRY
          catch (const std::exception&) {
            ENVOY_LOG_MISC(error, "Unexpected format in linux cgroup v2 cpu.stat file {}",
                           cgroupv2_cpu_stat_file_);
            return {false, true, 0, 0, 0};
          }
        }
        break;
      }
    }
    ENVOY_LOG_MISC(trace, "cgroupsv2 found_usage: {}", found_usage);
    if (!found_usage) {
      ENVOY_LOG_MISC(trace, "cgroupsv2 Missing usage_usec in linux cgroup v2 cpu.stat file {}",
                     cgroupv2_cpu_stat_file_);
      return {false, true, 0, 0, 0};
    }

    // Read cpuset.cpus.effective
    if (!v2_effective_file) {
      ENVOY_LOG_MISC(error, "Unexpected format in linux cgroup v2 cpuset.cpus.effective file {}",
                     cgroupv2_cpu_effective_file_);
      return {false, true, 0, 0, 0};
    }
    // Format can be a single CPU like "0" or a range like "0-3" or "0-16"
    int N = 0;
    std::string range_token;
    v2_effective_file >> range_token;
    if (!v2_effective_file) {
      ENVOY_LOG_MISC(error, "Unexpected format in linux cgroup v2 cpuset.cpus.effective file {}",
                     cgroupv2_cpu_effective_file_);
      return {false, true, 0, 0, 0};
    }
    size_t dash_pos = range_token.find('-');
    TRY_ASSERT_MAIN_THREAD {
      if (dash_pos == std::string::npos) {
        // Single CPU (e.g., "0" means 1 core)
        int single_cpu = std::stoi(range_token);
        if (single_cpu < 0) {
          ENVOY_LOG_MISC(error, "Invalid CPU value in {}: {}", cgroupv2_cpu_effective_file_,
                         range_token);
          return {false, true, 0, 0, 0};
        }
        N = 1;
      } else {
        // CPU range (e.g., "0-3" means 4 cores)
        int range_start = std::stoi(range_token.substr(0, dash_pos));
        int range_end = std::stoi(range_token.substr(dash_pos + 1));
        if (range_start < 0 || range_end < range_start) {
          ENVOY_LOG_MISC(error, "Invalid CPU range in {}: {}", cgroupv2_cpu_effective_file_,
                         range_token);
          return {false, true, 0, 0, 0};
        }
        N = (range_end - range_start + 1);
      }
    }
    END_TRY
    catch (const std::exception&) {
      ENVOY_LOG_MISC(error, "Unexpected numeric format in {}: {}", cgroupv2_cpu_effective_file_,
                     range_token);
      return {false, true, 0, 0, 0};
    }
    if (N <= 0) {
      ENVOY_LOG_MISC(error, "No CPUs found in {}", cgroupv2_cpu_effective_file_);
      return {false, true, 0, 0, 0};
    }
    ENVOY_LOG_MISC(trace, "cgroupsv2 calculated N from cpuset.cpus.effective: {}", N);

    // Read cpu.max
    if (!v2_max_file) {
      ENVOY_LOG_MISC(error, "Unexpected format in linux cgroup v2 cpu.max file {}",
                     cgroupv2_cpu_max_file_);
      return {false, true, 0, 0, 0};
    }
    double effective_cores = 0;
    std::string quota_str, period_str;
    v2_max_file >> quota_str >> period_str;
    if (!v2_max_file) {
      ENVOY_LOG_MISC(error, "Unexpected format in linux cgroup v2 cpu.max file {}",
                     cgroupv2_cpu_max_file_);
      return {false, true, 0, 0, 0};
    }

    if (quota_str == "max") {
      ENVOY_LOG_MISC(trace, "cgroupsv2 max quota found using N: {}", N);
      effective_cores = static_cast<double>(N);
    } else {
      TRY_ASSERT_MAIN_THREAD {
        const int quota = std::stoi(quota_str);
        const int period = std::stoi(period_str);
        if (period <= 0) {
          ENVOY_LOG_MISC(error, "Invalid period value in {}: {}", cgroupv2_cpu_max_file_,
                         period_str);
          return {false, true, 0, 0, 0};
        }
        const double q_cores = static_cast<double>(quota) / static_cast<double>(period);
        effective_cores = std::min(static_cast<double>(N), q_cores);
      }
      END_TRY
      catch (const std::exception&) {
        ENVOY_LOG_MISC(error, "Unexpected numeric format in {}: {} {}", cgroupv2_cpu_max_file_,
                       quota_str, period_str);
        return {false, true, 0, 0, 0};
      }
    }

    // Convert usage from usec to nsec
    const double cpu_times_value_ns = static_cast<double>(usage_usec) * 1000.0;
    const double cpu_times_value_us = static_cast<double>(usage_usec);

    const uint64_t current_time = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                      time_source_.monotonicTime().time_since_epoch())
                                      .count();
    ENVOY_LOG_MISC(trace, "cgroupsv2 cpu_times_value_ns: {}", cpu_times_value_ns);
    ENVOY_LOG_MISC(trace, "cgroupsv2 cpu_times_value_us: {}", cpu_times_value_us);
    ENVOY_LOG_MISC(trace, "cgroupsv2 current_time: {}", current_time);

    // Return usage in nanoseconds to match cgroup v1 units
    return {true, true, cpu_times_value_us, current_time, effective_cores};
  }
  ENVOY_LOG_MISC(trace, "cgroupsv1 cgroup v2 not found");
  ENVOY_LOG_MISC(trace, "cgroupsv1 failing back to cgroup v1 ");

  // cgroupv1 fallback
  std::ifstream cpu_allocated_file, cpu_times_file;
  double cpu_allocated_value, cpu_times_value;

  cpu_allocated_file.open(linux_cgroup_cpu_allocated_file_);
  if (!cpu_allocated_file.is_open()) {
    ENVOY_LOG_MISC(error, "Can't open linux cpu allocated file {}",
                   linux_cgroup_cpu_allocated_file_);
    return {false, false, 0, 0, 0};
  }

  cpu_times_file.open(linux_cgroup_cpu_times_file_);
  if (!cpu_times_file.is_open()) {
    ENVOY_LOG_MISC(error, "Can't open linux cpu usage seconds file {}",
                   linux_cgroup_cpu_times_file_);
    return {false, false, 0, 0, 0};
  }

  cpu_allocated_file >> cpu_allocated_value;
  if (!cpu_allocated_file) {
    ENVOY_LOG_MISC(error, "Unexpected format in linux cpu allocated file {}",
                   linux_cgroup_cpu_allocated_file_);
    return {false, false, 0, 0, 0};
  }

  cpu_times_file >> cpu_times_value;
  if (!cpu_times_file) {
    ENVOY_LOG_MISC(error, "Unexpected format in linux cpu usage seconds file {}",
                   linux_cgroup_cpu_times_file_);
    return {false, false, 0, 0, 0};
  }

  const uint64_t current_time = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                    time_source_.monotonicTime().time_since_epoch())
                                    .count();
  ENVOY_LOG_MISC(trace, "cgroupsv1 cpu_times_value: {}", cpu_times_value);
  ENVOY_LOG_MISC(trace, "cgroupsv1 cpu_allocated_value: {}", cpu_allocated_value);
  ENVOY_LOG_MISC(trace, "cgroupsv1 current_time: {}", current_time);
  return {true, false, (cpu_times_value * CONTAINER_MILLICORES_PER_CORE) / cpu_allocated_value,
          current_time, 0}; // cpu_times is in nanoseconds and cpu_allocated shares is in Millicores
}

} // namespace CpuUtilizationMonitor
} // namespace ResourceMonitors
} // namespace Extensions
} // namespace Envoy