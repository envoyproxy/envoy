#pragma once

#include "envoy/extensions/resource_monitors/envoy_container_cpu_utilization/v3/envoy_container_cpu_utilization.pb.h"
#include "envoy/extensions/resource_monitors/envoy_container_cpu_utilization/v3/envoy_container_cpu_utilization.pb.validate.h"
#include "envoy/server/resource_monitor_config.h"

#include "source/extensions/resource_monitors/common/factory_base.h"

namespace Envoy {
namespace Extensions {
namespace ResourceMonitors {
namespace EnvoyContainerCpuUtilizationMonitor {

class EnvoyContainerCpuUtilizationMonitorFactory
    : public Common::FactoryBase<
          envoy::extensions::resource_monitors::envoy_container_cpu_utilization::v3::EnvoyContainerCpuUtilizationConfig> {
public:
  EnvoyContainerCpuUtilizationMonitorFactory() : FactoryBase("envoy.resource_monitors.envoy_container_cpu_utilization") {}

private:
  Server::ResourceMonitorPtr createResourceMonitorFromProtoTyped(
      const envoy::extensions::resource_monitors::envoy_container_cpu_utilization::v3::EnvoyContainerCpuUtilizationConfig& config,
      Server::Configuration::ResourceMonitorFactoryContext& context) override;
};

} // namespace EnvoyContainerCpuUtilizationMonitor
} // namespace ResourceMonitors
} // namespace Extensions
} // namespace Envoy
