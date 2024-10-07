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

class CpuStatsReader {
public:
  CpuStatsReader() = default;
  virtual ~CpuStatsReader() = default;
  virtual CpuTimes getCpuTimes() = 0;
};

} // namespace CpuUtilizationMonitor
} // namespace ResourceMonitors
} // namespace Extensions
} // namespace Envoy
