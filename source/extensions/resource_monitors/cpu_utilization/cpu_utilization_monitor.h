#pragma once

#include <chrono>

#include "envoy/extensions/resource_monitors/cpu_utilization/v3/cpu_utilization.pb.h"
#include "envoy/server/resource_monitor.h"

#include "source/common/runtime/runtime_features.h"
#include "source/extensions/resource_monitors/cpu_utilization/cpu_stats_reader.h"

namespace Envoy {
namespace Extensions {
namespace ResourceMonitors {
namespace CpuUtilizationMonitor {

class CpuUtilizationMonitor : public Server::ResourceMonitor {
public:
  CpuUtilizationMonitor(
      const envoy::extensions::resource_monitors::cpu_utilization::v3::CpuUtilizationConfig& config,
      std::unique_ptr<CpuStatsReader> cpu_stats_reader);

  void updateResourceUsage(Server::ResourceUpdateCallbacks& callbacks) override;

private:
  double utilization_;
  CpuTimes previous_cpu_times_;
  std::unique_ptr<CpuStatsReader> cpu_stats_reader_;
};

} // namespace CpuUtilizationMonitor
} // namespace ResourceMonitors
} // namespace Extensions
} // namespace Envoy
