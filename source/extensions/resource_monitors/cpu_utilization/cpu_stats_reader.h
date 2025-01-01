#pragma once

#include <dirent.h>
#include <unistd.h>

#include <algorithm>
#include <filesystem>
#include <fstream>

#include "source/common/common/logger.h"

#include "absl/strings/str_split.h"

namespace Envoy {
namespace Extensions {
namespace ResourceMonitors {
namespace CpuUtilizationMonitor {

struct CpuTimes {
  bool is_valid;
  uint64_t work_time;
  uint64_t total_time;
};

struct CgroupStats {
  bool is_valid;
  uint64_t cpu_allocated_millicores_; //total millicores of cpu allocated to container
  uint64_t total_cpu_times_ns_; //total cpu times in nanoseconds
};

class CpuStatsReader {
public:
  CpuStatsReader() = default;
  virtual ~CpuStatsReader() = default;
  virtual CpuTimes getCpuTimes() = 0;
};

class CgroupStatsReader {
public:
  CgroupStatsReader() = default;
  virtual ~CgroupStatsReader() = default;
  virtual CgroupStats getCgroupStats() = 0;
};

} // namespace CpuUtilizationMonitor
} // namespace ResourceMonitors
} // namespace Extensions
} // namespace Envoy
