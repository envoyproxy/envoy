#include "source/extensions/resource_monitors/cpu_utilization/linux_cpu_stats_reader.h"

#include <algorithm>
#include <chrono>
#include <sstream>
#include <vector>

#include "envoy/common/exception.h"
#include "envoy/common/time.h"

#include "source/common/common/assert.h"
#include "source/common/common/fmt.h"
#include "source/common/common/thread.h"

namespace Envoy {
namespace Extensions {
namespace ResourceMonitors {
namespace CpuUtilizationMonitor {

constexpr uint64_t NUMBER_OF_CPU_TIMES_TO_PARSE =
    4; // we are interested in user, nice, system and idle times.

// ================================================================================
// LinuxCpuStatsReader (Host-level CPU monitoring) - ORIGINAL IMPLEMENTATION
// ================================================================================

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

// ================================================================================
// Factory Method for Container CPU Stats Reader
// ================================================================================

LinuxContainerCpuStatsReader::ContainerStatsReaderPtr
LinuxContainerCpuStatsReader::create(Filesystem::Instance& fs, TimeSource& time_source) {
  // Check if host supports cgroup v2
  if (CpuPaths::isV2(fs)) {
    return std::make_unique<CgroupV2CpuStatsReader>(fs, time_source);
  }

  // Check if host supports cgroup v1
  if (CpuPaths::isV1(fs)) {
    return std::make_unique<CgroupV1CpuStatsReader>(fs, time_source);
  }

  throw EnvoyException("No supported cgroup CPU implementation found");
}

// ================================================================================
// CgroupV1CpuStatsReader Implementation
// ================================================================================

CgroupV1CpuStatsReader::CgroupV1CpuStatsReader(Filesystem::Instance& fs, TimeSource& time_source)
    : LinuxContainerCpuStatsReader(fs, time_source), shares_path_(CpuPaths::V1::getSharesPath()),
      usage_path_(CpuPaths::V1::getUsagePath()) {}

CgroupV1CpuStatsReader::CgroupV1CpuStatsReader(Filesystem::Instance& fs, TimeSource& time_source,
                                               const std::string& shares_path,
                                               const std::string& usage_path)
    : LinuxContainerCpuStatsReader(fs, time_source), shares_path_(shares_path),
      usage_path_(usage_path) {}

CpuTimes CgroupV1CpuStatsReader::getCpuTimes() {
  // Read cpu.shares (cpu allocated)
  auto shares_result = fs_.fileReadToEnd(shares_path_);
  if (!shares_result.ok()) {
    ENVOY_LOG(error, "Unable to read CPU shares file at {}", shares_path_);
    return {false, false, 0, 0, 0};
  }

  // Read cpuacct.usage (cpu times)
  auto usage_result = fs_.fileReadToEnd(usage_path_);
  if (!usage_result.ok()) {
    ENVOY_LOG(error, "Unable to read CPU usage file at {}", usage_path_);
    return {false, false, 0, 0, 0};
  }

  TRY_ASSERT_MAIN_THREAD {
    const double cpu_allocated_value = std::stod(shares_result.value());
    const double cpu_times_value = std::stod(usage_result.value());

    if (cpu_allocated_value <= 0) {
      ENVOY_LOG(error, "Invalid CPU shares value: {}", cpu_allocated_value);
      return {false, false, 0, 0, 0};
    }

    const uint64_t current_time = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                      time_source_.monotonicTime().time_since_epoch())
                                      .count();

    // cpu_times is in nanoseconds, cpu_allocated shares is in millicores
    const double work_time =
        (cpu_times_value * CONTAINER_MILLICORES_PER_CORE) / cpu_allocated_value;

    ENVOY_LOG(trace, "cgroupv1 cpu_times_value: {}", cpu_times_value);
    ENVOY_LOG(trace, "cgroupv1 cpu_allocated_value: {}", cpu_allocated_value);
    ENVOY_LOG(trace, "cgroupv1 current_time: {}", current_time);

    return {true, false, work_time, current_time, 0};
  }
  END_TRY
  catch (const std::exception& e) {
    ENVOY_LOG(error, "Failed to parse cgroup v1 CPU stats: {}", e.what());
    return {false, false, 0, 0, 0};
  }
}

// ================================================================================
// CgroupV2CpuStatsReader Implementation
// ================================================================================

CgroupV2CpuStatsReader::CgroupV2CpuStatsReader(Filesystem::Instance& fs, TimeSource& time_source)
    : LinuxContainerCpuStatsReader(fs, time_source), stat_path_(CpuPaths::V2::getStatPath()),
      max_path_(CpuPaths::V2::getMaxPath()), effective_path_(CpuPaths::V2::getEffectiveCpusPath()) {
}

CgroupV2CpuStatsReader::CgroupV2CpuStatsReader(Filesystem::Instance& fs, TimeSource& time_source,
                                               const std::string& stat_path,
                                               const std::string& max_path,
                                               const std::string& effective_path)
    : LinuxContainerCpuStatsReader(fs, time_source), stat_path_(stat_path), max_path_(max_path),
      effective_path_(effective_path) {}

CpuTimes CgroupV2CpuStatsReader::getCpuTimes() {
  // Read cpu.stat for usage_usec
  auto stat_result = fs_.fileReadToEnd(stat_path_);
  if (!stat_result.ok()) {
    ENVOY_LOG(error, "Unable to read CPU stat file at {}", stat_path_);
    return {false, true, 0, 0, 0};
  }

  // Parse usage_usec from cpu.stat
  uint64_t usage_usec = 0;
  bool found_usage = false;
  std::istringstream stat_stream(stat_result.value());
  std::string line;

  while (std::getline(stat_stream, line)) {
    if (line.rfind("usage_usec ", 0) == 0) {
      // Line starts with "usage_usec "
      const size_t pos = line.find_last_of(' ');
      if (pos != std::string::npos) {
        TRY_ASSERT_MAIN_THREAD {
          usage_usec = std::stoull(line.substr(pos + 1));
          found_usage = true;
        }
        END_TRY
        catch (const std::exception&) {
          ENVOY_LOG(error, "Unexpected format in cpu.stat file {}", stat_path_);
          return {false, true, 0, 0, 0};
        }
      }
      break;
    }
  }

  if (!found_usage) {
    ENVOY_LOG(trace, "Missing usage_usec in cpu.stat file {}", stat_path_);
    return {false, true, 0, 0, 0};
  }

  // Read cpuset.cpus.effective
  auto effective_result = fs_.fileReadToEnd(effective_path_);
  if (!effective_result.ok()) {
    ENVOY_LOG(error, "Unable to read effective CPUs file at {}", effective_path_);
    return {false, true, 0, 0, 0};
  }

  // Parse effective CPUs (format: "0" or "0-3")
  int N = 0;
  std::string range_token = effective_result.value();
  // Trim whitespace
  range_token.erase(range_token.find_last_not_of(" \n\r\t") + 1);

  const size_t dash_pos = range_token.find('-');
  TRY_ASSERT_MAIN_THREAD {
    if (dash_pos == std::string::npos) {
      // Single CPU (e.g., "0" means 1 core)
      const int single_cpu = std::stoi(range_token);
      if (single_cpu < 0) {
        ENVOY_LOG(error, "Invalid CPU value in {}: {}", effective_path_, range_token);
        return {false, true, 0, 0, 0};
      }
      N = 1;
    } else {
      // CPU range (e.g., "0-3" means 4 cores)
      const int range_start = std::stoi(range_token.substr(0, dash_pos));
      const int range_end = std::stoi(range_token.substr(dash_pos + 1));
      if (range_start < 0 || range_end < range_start) {
        ENVOY_LOG(error, "Invalid CPU range in {}: {}", effective_path_, range_token);
        return {false, true, 0, 0, 0};
      }
      N = (range_end - range_start + 1);
    }
  }
  END_TRY
  catch (const std::exception&) {
    ENVOY_LOG(error, "Unexpected numeric format in {}: {}", effective_path_, range_token);
    return {false, true, 0, 0, 0};
  }

  if (N <= 0) {
    ENVOY_LOG(error, "No CPUs found in {}", effective_path_);
    return {false, true, 0, 0, 0};
  }

  // Read cpu.max
  auto max_result = fs_.fileReadToEnd(max_path_);
  if (!max_result.ok()) {
    ENVOY_LOG(error, "Unable to read CPU max file at {}", max_path_);
    return {false, true, 0, 0, 0};
  }

  // Parse cpu.max (format: "quota period" or "max period")
  double effective_cores = 0;
  std::istringstream max_stream(max_result.value());
  std::string quota_str, period_str;
  max_stream >> quota_str >> period_str;

  if (!max_stream) {
    ENVOY_LOG(error, "Unexpected format in cpu.max file {}", max_path_);
    return {false, true, 0, 0, 0};
  }

  if (quota_str == "max") {
    ENVOY_LOG(trace, "cgroupv2 max quota found, using N: {}", N);
    effective_cores = static_cast<double>(N);
  } else {
    TRY_ASSERT_MAIN_THREAD {
      const int quota = std::stoi(quota_str);
      const int period = std::stoi(period_str);
      if (period <= 0) {
        ENVOY_LOG(error, "Invalid period value in {}: {}", max_path_, period_str);
        return {false, true, 0, 0, 0};
      }
      const double q_cores = static_cast<double>(quota) / static_cast<double>(period);
      effective_cores = std::min(static_cast<double>(N), q_cores);
    }
    END_TRY
    catch (const std::exception&) {
      ENVOY_LOG(error, "Unexpected numeric format in {}: {} {}", max_path_, quota_str, period_str);
      return {false, true, 0, 0, 0};
    }
  }

  // Convert usage from usec to match our time units
  const double cpu_times_value_us = static_cast<double>(usage_usec);
  const uint64_t current_time = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                    time_source_.monotonicTime().time_since_epoch())
                                    .count();

  ENVOY_LOG(trace, "cgroupv2 usage_usec: {}", usage_usec);
  ENVOY_LOG(trace, "cgroupv2 effective_cores: {}", effective_cores);
  ENVOY_LOG(trace, "cgroupv2 current_time: {}", current_time);

  return {true, true, cpu_times_value_us, current_time, effective_cores};
}

} // namespace CpuUtilizationMonitor
} // namespace ResourceMonitors
} // namespace Extensions
} // namespace Envoy
