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

#include "absl/strings/numbers.h"
#include "absl/strings/str_split.h"
#include "absl/strings/strip.h"

namespace Envoy {
namespace Extensions {
namespace ResourceMonitors {
namespace CpuUtilizationMonitor {

constexpr uint64_t NUMBER_OF_CPU_TIMES_TO_PARSE =
    4; // we are interested in user, nice, system and idle times.

namespace {

absl::StatusOr<int> parseEffectiveCpus(absl::string_view effective_cpu_list,
                                       const std::string& effective_path) {
  int cpu_count = 0;
  std::string cpu_list = std::string(absl::StripTrailingAsciiWhitespace(effective_cpu_list));

  // Split by comma to handle multiple ranges/individual CPUs
  std::vector<std::string> tokens = absl::StrSplit(cpu_list, ',');
  for (const auto& token : tokens) {
    const size_t dash_pos = token.find('-');
    if (dash_pos == std::string::npos) {
      // Single CPU (e.g., "0" or "4")
      int single_cpu;
      if (!absl::SimpleAtoi(token, &single_cpu)) {
        return absl::InvalidArgumentError("Failed to parse CPU value");
      }
      if (single_cpu < 0) {
        return absl::InvalidArgumentError("Invalid CPU value");
      }
      cpu_count += 1;
    } else {
      // CPU range (e.g., "0-3" means 4 cores)
      int range_start, range_end;
      if (!absl::SimpleAtoi(token.substr(0, dash_pos), &range_start) ||
          !absl::SimpleAtoi(token.substr(dash_pos + 1), &range_end)) {
        return absl::InvalidArgumentError("Failed to parse CPU range");
      }
      if (range_start < 0 || range_end < range_start) {
        return absl::InvalidArgumentError("Invalid CPU range");
      }
      cpu_count += (range_end - range_start + 1);
    }
  }

  if (cpu_count <= 0) {
    ENVOY_LOG_MISC(error, "No CPUs found in {}", effective_path);
    return absl::InvalidArgumentError("No CPUs found");
  }

  return cpu_count;
}

absl::StatusOr<double> parseEffectiveCores(absl::string_view cpu_max_contents, int cpu_count) {
  // Parse cpu.max (format: "quota period" or "max period")
  std::istringstream max_stream{std::string(cpu_max_contents)};
  std::string quota_str, period_str;
  max_stream >> quota_str >> period_str;

  if (!max_stream) {
    return absl::InvalidArgumentError("Unexpected cpu.max format");
  }

  if (quota_str == "max") {
    return static_cast<double>(cpu_count);
  }

  int quota, period;
  if (!absl::SimpleAtoi(quota_str, &quota) || !absl::SimpleAtoi(period_str, &period)) {
    return absl::InvalidArgumentError("Failed to parse cpu.max values");
  }
  if (period <= 0) {
    return absl::InvalidArgumentError("Invalid cpu.max period");
  }

  const double q_cores = static_cast<double>(quota) / static_cast<double>(period);
  return std::min(static_cast<double>(cpu_count), q_cores);
}

} // namespace

// LinuxCpuStatsReader (Host-level CPU monitoring)
LinuxCpuStatsReader::LinuxCpuStatsReader(const std::string& cpu_stats_filename)
    : cpu_stats_filename_(cpu_stats_filename) {}

CpuTimesBase LinuxCpuStatsReader::getCpuTimes() {
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

absl::StatusOr<double> LinuxCpuStatsReader::getUtilization() {
  CpuTimesBase current_cpu_times = getCpuTimes();

  if (!current_cpu_times.is_valid) {
    return absl::InvalidArgumentError("Failed to read CPU times");
  }

  // For the first call, initialize previous times and return 0
  if (!previous_cpu_times_.is_valid) {
    previous_cpu_times_ = current_cpu_times;
    return 0.0;
  }

  const double work_over_period = current_cpu_times.work_time - previous_cpu_times_.work_time;
  const int64_t total_over_period = current_cpu_times.total_time - previous_cpu_times_.total_time;

  if (work_over_period < 0 || total_over_period <= 0) {
    return absl::InvalidArgumentError(
        fmt::format("Erroneous CPU stats calculation. Work_over_period='{}' cannot "
                    "be a negative number and total_over_period='{}' must be a positive number.",
                    work_over_period, total_over_period));
  }

  const double utilization = work_over_period / total_over_period;

  // Update previous times for the next call
  previous_cpu_times_ = current_cpu_times;

  return utilization;
}

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

CgroupV1CpuStatsReader::CgroupV1CpuStatsReader(Filesystem::Instance& fs, TimeSource& time_source)
    : LinuxContainerCpuStatsReader(fs, time_source), shares_path_(CpuPaths::V1::getSharesPath()),
      usage_path_(CpuPaths::V1::getUsagePath()) {}

CgroupV1CpuStatsReader::CgroupV1CpuStatsReader(Filesystem::Instance& fs, TimeSource& time_source,
                                               const std::string& shares_path,
                                               const std::string& usage_path)
    : LinuxContainerCpuStatsReader(fs, time_source), shares_path_(shares_path),
      usage_path_(usage_path) {}

CpuTimesBase CgroupV1CpuStatsReader::getCpuTimes() {
  // Read cpu.shares (cpu allocated)
  auto shares_result = fs_.fileReadToEnd(shares_path_);
  if (!shares_result.ok()) {
    ENVOY_LOG(error, "Unable to read CPU shares file at {}", shares_path_);
    return {false, 0, 0};
  }

  // Read cpuacct.usage (cpu times)
  auto usage_result = fs_.fileReadToEnd(usage_path_);
  if (!usage_result.ok()) {
    ENVOY_LOG(error, "Unable to read CPU usage file at {}", usage_path_);
    return {false, 0, 0};
  }

  double cpu_allocated_value;
  if (!absl::SimpleAtod(shares_result.value(), &cpu_allocated_value)) {
    ENVOY_LOG(error, "Failed to parse CPU shares value: {}", shares_result.value());
    return {false, 0, 0};
  }

  double cpu_times_value;
  if (!absl::SimpleAtod(usage_result.value(), &cpu_times_value)) {
    ENVOY_LOG(error, "Failed to parse CPU usage value: {}", usage_result.value());
    return {false, 0, 0};
  }

  if (cpu_allocated_value <= 0) {
    ENVOY_LOG(error, "Invalid CPU shares value: {}", cpu_allocated_value);
    return {false, 0, 0};
  }

  const uint64_t current_time = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                    time_source_.monotonicTime().time_since_epoch())
                                    .count();

  // cpu_times is in nanoseconds, cpu_allocated shares is in millicores
  const double work_time = (cpu_times_value * CONTAINER_MILLICORES_PER_CORE) / cpu_allocated_value;

  ENVOY_LOG(trace, "cgroupv1 cpu_times_value: {}, cpu_allocated_value: {}, current_time: {}",
            cpu_times_value, cpu_allocated_value, current_time);

  return {true, work_time, current_time};
}

absl::StatusOr<double> CgroupV1CpuStatsReader::getUtilization() {
  CpuTimesBase current_cpu_times = getCpuTimes();

  if (!current_cpu_times.is_valid) {
    return absl::InvalidArgumentError("Failed to read CPU times");
  }

  if (!previous_cpu_times_.is_valid) {
    previous_cpu_times_ = current_cpu_times;
    return 0.0;
  }

  const double work_over_period = current_cpu_times.work_time - previous_cpu_times_.work_time;
  const int64_t total_over_period = current_cpu_times.total_time - previous_cpu_times_.total_time;

  if (work_over_period < 0 || total_over_period <= 0) {
    return absl::InvalidArgumentError(
        fmt::format("Erroneous CPU stats calculation. Work_over_period='{}' cannot "
                    "be a negative number and total_over_period='{}' must be a positive number.",
                    work_over_period, total_over_period));
  }

  const double utilization = work_over_period / total_over_period;

  previous_cpu_times_ = current_cpu_times;

  return utilization;
}

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

CpuTimesV2 CgroupV2CpuStatsReader::getCpuTimes() {
  // Read cpu.stat for usage_usec
  auto stat_result = fs_.fileReadToEnd(stat_path_);
  if (!stat_result.ok()) {
    ENVOY_LOG(error, "Unable to read CPU stat file at {}", stat_path_);
    return {false, 0, 0, 0};
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
        if (!absl::SimpleAtoi(line.substr(pos + 1), &usage_usec)) {
          ENVOY_LOG(error, "Failed to parse usage_usec in cpu.stat file {}", stat_path_);
          return {false, 0, 0, 0};
        }
        found_usage = true;
      }
      break;
    }
  }

  if (!found_usage) {
    ENVOY_LOG(trace, "Missing usage_usec in cpu.stat file {}", stat_path_);
    return {false, 0, 0, 0};
  }

  // Read cpuset.cpus.effective
  auto effective_result = fs_.fileReadToEnd(effective_path_);
  if (!effective_result.ok()) {
    ENVOY_LOG(error, "Unable to read effective CPUs file at {}", effective_path_);
    return {false, 0, 0, 0};
  }

  // Parse effective CPUs
  // Format can be: "0", "0-3", "0,2,4", "0-2,4", "0-3,5-7", etc.
  absl::StatusOr<int> cpu_count = parseEffectiveCpus(effective_result.value(), effective_path_);
  if (!cpu_count.ok()) {
    ENVOY_LOG(error, "Failed to parse effective CPUs file {}: {}", effective_path_,
              cpu_count.status().message());
    return {false, 0, 0, 0};
  }
  const int N = cpu_count.value();

  // Read cpu.max
  auto max_result = fs_.fileReadToEnd(max_path_);
  if (!max_result.ok()) {
    ENVOY_LOG(error, "Unable to read CPU max file at {}", max_path_);
    return {false, 0, 0, 0};
  }

  absl::StatusOr<double> effective_cores = parseEffectiveCores(max_result.value(), N);
  if (!effective_cores.ok()) {
    ENVOY_LOG(error, "Failed to parse cpu.max file {}: {}", max_path_,
              effective_cores.status().message());
    return {false, 0, 0, 0};
  }

  // Convert usage from usec to match our time units
  const double cpu_times_value_us = static_cast<double>(usage_usec);
  const uint64_t current_time = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                    time_source_.monotonicTime().time_since_epoch())
                                    .count();

  ENVOY_LOG(trace, "cgroupv2 usage_usec: {}, effective_cores: {}, current_time: {}", usage_usec,
            effective_cores.value(), current_time);

  return {true, cpu_times_value_us, current_time, effective_cores.value()};
}

absl::StatusOr<double> CgroupV2CpuStatsReader::getUtilization() {
  CpuTimesV2 current_cpu_times = getCpuTimes();

  if (!current_cpu_times.is_valid) {
    return absl::InvalidArgumentError("Failed to read CPU times");
  }

  // For the first call, initialize previous times and return 0
  if (!previous_cpu_times_.is_valid) {
    previous_cpu_times_ = current_cpu_times;
    return 0.0;
  }

  // CgroupV2-specific calculation with unit conversions and effective cores
  const double work_over_period = current_cpu_times.work_time - previous_cpu_times_.work_time;
  const int64_t total_over_period = current_cpu_times.total_time - previous_cpu_times_.total_time;

  if (work_over_period < 0 || total_over_period <= 0) {
    return absl::InvalidArgumentError(
        fmt::format("Erroneous CPU stats calculation. Work_over_period='{}' cannot "
                    "be a negative number and total_over_period='{}' must be a positive number.",
                    work_over_period, total_over_period));
  }

  // Convert nanoseconds to seconds and microseconds to seconds
  const double total_over_period_seconds = total_over_period / 1000000000.0;
  const double work_over_period_seconds = work_over_period / 1000000.0;

  // Calculate utilization considering effective cores
  const double utilization =
      work_over_period_seconds / (total_over_period_seconds * current_cpu_times.effective_cores);

  // Clamp to [0.0, 1.0]
  const double clamped_utilization = std::clamp(utilization, 0.0, 1.0);

  // Update previous times for next call
  previous_cpu_times_ = current_cpu_times;

  return clamped_utilization;
}

} // namespace CpuUtilizationMonitor
} // namespace ResourceMonitors
} // namespace Extensions
} // namespace Envoy
