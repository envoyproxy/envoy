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
  double work_time;
  double total_time;
};

class CpuStatsReader {
public:
  CpuStatsReader() = default;
  virtual ~CpuStatsReader() = default;
  virtual CpuTimes getCpuTimes() PURE;
};

class CgroupStatsReader {
public:
  CgroupStatsReader() = default;
  virtual ~CgroupStatsReader() = default;
  virtual CpuTimes getCgroupStats() PURE;
};

} // namespace CpuUtilizationMonitor
} // namespace ResourceMonitors
} // namespace Extensions
} // namespace Envoy
