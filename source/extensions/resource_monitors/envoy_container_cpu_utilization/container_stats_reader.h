#pragma once

#include <chrono>
#include "source/common/common/logger.h"
#include "source/common/common/utility.h"

#include "absl/strings/str_split.h"

namespace Envoy {
namespace Extensions {
namespace ResourceMonitors {
namespace EnvoyContainerCpuUtilizationMonitor {


struct EnvoyContainerStats {
  bool is_valid;
  int64_t cpu_allocated_millicores_; //total millicores of cpu allocated to container
  int64_t total_cpu_times_ns_; //total cpu times in nanoseconds
  std::chrono::time_point<std::chrono::system_clock> system_time_;
};


class ContainerStatsReader {
public:
  ContainerStatsReader() = default;
  virtual ~ContainerStatsReader() = default;
  virtual EnvoyContainerStats getEnvoyContainerStats() = 0;
  std::chrono::time_point<std::chrono::system_clock> get_system_time_epoch(){ return std::chrono::system_clock::now(); }
};

} // namespace EnvoyContainerCpuUtilizationMonitor
} // namespace ResourceMonitors
} // namespace Extensions
} // namespace Envoy
