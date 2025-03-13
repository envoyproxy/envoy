#pragma once

#include "envoy/extensions/resource_monitors/cpu_utilization/v3/cpu_utilization.pb.h"
#include "envoy/extensions/resource_monitors/cpu_utilization/v3/cpu_utilization.pb.validate.h"
#include "envoy/server/resource_monitor_config.h"

#include "source/extensions/resource_monitors/common/factory_base.h"

namespace Envoy {
namespace Extensions {
namespace ResourceMonitors {
namespace CpuUtilizationMonitor {

class CpuUtilizationMonitorFactory
    : public Common::FactoryBase<
          envoy::extensions::resource_monitors::cpu_utilization::v3::CpuUtilizationConfig> {
public:
  CpuUtilizationMonitorFactory() : FactoryBase("envoy.resource_monitors.cpu_utilization") {}

private:
  Server::ResourceMonitorPtr createResourceMonitorFromProtoTyped(
      const envoy::extensions::resource_monitors::cpu_utilization::v3::CpuUtilizationConfig& config,
      Server::Configuration::ResourceMonitorFactoryContext& context) override;
};

} // namespace CpuUtilizationMonitor
} // namespace ResourceMonitors
} // namespace Extensions
} // namespace Envoy
