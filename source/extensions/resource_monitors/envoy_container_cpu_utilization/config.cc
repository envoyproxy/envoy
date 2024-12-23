#include "source/extensions/resource_monitors/envoy_container_cpu_utilization/config.h"

#include "envoy/extensions/resource_monitors/envoy_container_cpu_utilization/v3/envoy_container_cpu_utilization.pb.h"
#include "envoy/extensions/resource_monitors/envoy_container_cpu_utilization/v3/envoy_container_cpu_utilization.pb.validate.h"
#include "envoy/registry/registry.h"

#include "source/common/protobuf/utility.h"
#include "source/extensions/resource_monitors/envoy_container_cpu_utilization/envoy_container_cpu_utilization_monitor.h"
#include "source/extensions/resource_monitors/envoy_container_cpu_utilization/linux_container_stats_reader.h"

namespace Envoy {
namespace Extensions {
namespace ResourceMonitors {
namespace EnvoyContainerCpuUtilizationMonitor {

Server::ResourceMonitorPtr EnvoyContainerCpuUtilizationMonitorFactory::createResourceMonitorFromProtoTyped(
    const envoy::extensions::resource_monitors::envoy_container_cpu_utilization::v3::EnvoyContainerCpuUtilizationConfig& config,
    Server::Configuration::ResourceMonitorFactoryContext& /*unused_context*/) {
  // In the future, the below can be configurable based on the operating system.
  auto cpu_stats_reader = std::make_unique<LinuxContainerStatsReader>();
  return std::make_unique<EnvoyContainerCpuUtilizationMonitor>(config, std::move(cpu_stats_reader));
}

/**
 * Static registration for the container cpu resource monitor factory. @see RegistryFactory.
 */
REGISTER_FACTORY(EnvoyContainerCpuUtilizationMonitorFactory, Server::Configuration::ResourceMonitorFactory);

} // namespace EnvoyContainerCpuUtilizationMonitor
} // namespace ResourceMonitors
} // namespace Extensions
} // namespace Envoy
