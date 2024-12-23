#pragma once


#include "envoy/extensions/resource_monitors/envoy_container_cpu_utilization/v3/envoy_container_cpu_utilization.pb.h"
#include "envoy/server/resource_monitor.h"

#include "source/common/runtime/runtime_features.h"
#include "source/extensions/resource_monitors/envoy_container_cpu_utilization/container_stats_reader.h"

namespace Envoy {
namespace Extensions {
namespace ResourceMonitors {
namespace EnvoyContainerCpuUtilizationMonitor {

class EnvoyContainerCpuUtilizationMonitor : public Server::ResourceMonitor {
public:
  EnvoyContainerCpuUtilizationMonitor(
      const envoy::extensions::resource_monitors::envoy_container_cpu_utilization::v3::EnvoyContainerCpuUtilizationConfig& config,
      std::unique_ptr<ContainerStatsReader> envoy_container_stats_reader);

  void updateResourceUsage(Server::ResourceUpdateCallbacks& callbacks) override;

private:
  double utilization_ = 0.0;
  EnvoyContainerStats previous_envoy_container_stats_;
  std::unique_ptr<ContainerStatsReader> envoy_container_stats_reader_;
};

} // namespace EnvoyContainerCpuUtilizationMonitor
} // namespace ResourceMonitors
} // namespace Extensions
} // namespace Envoy
