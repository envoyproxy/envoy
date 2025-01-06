#include "source/extensions/resource_monitors/cpu_utilization/config.h"
#include "envoy/common/time.h"
#include "envoy/extensions/resource_monitors/cpu_utilization/v3/cpu_utilization.pb.h"
#include "envoy/extensions/resource_monitors/cpu_utilization/v3/cpu_utilization.pb.validate.h"
#include "envoy/registry/registry.h"
#include "source/common/protobuf/utility.h"
#include "source/extensions/resource_monitors/cpu_utilization/cpu_utilization_monitor.h"
#include "source/extensions/resource_monitors/cpu_utilization/linux_cpu_stats_reader.h"

namespace Envoy {
namespace Extensions {
namespace ResourceMonitors {
namespace CpuUtilizationMonitor {

Server::ResourceMonitorPtr CpuUtilizationMonitorFactory::createResourceMonitorFromProtoTyped(
    const envoy::extensions::resource_monitors::cpu_utilization::v3::CpuUtilizationConfig& config,
    Server::Configuration::ResourceMonitorFactoryContext& context) {
  // In the future, the below can be configurable based on the operating system.
  TimeSource& time_source = context.api().timeSource();
  if (config.mode() == envoy::extensions::resource_monitors::cpu_utilization::v3::CpuUtilizationConfig::CONTAINER) {
    auto cgroup_stats_reader = std::make_unique<LinuxContainerCpuStatsReader>();
    return std::make_unique<CpuUtilizationMonitor>(config, std::move(cgroup_stats_reader), time_source);
  }
  auto cpu_stats_reader = std::make_unique<LinuxCpuStatsReader>();
  return std::make_unique<CpuUtilizationMonitor>(config, std::move(cpu_stats_reader), time_source);
}

/**
 * Static registration for the cpu resource monitor factory. @see RegistryFactory.
 */
REGISTER_FACTORY(CpuUtilizationMonitorFactory, Server::Configuration::ResourceMonitorFactory);

} // namespace CpuUtilizationMonitor
} // namespace ResourceMonitors
} // namespace Extensions
} // namespace Envoy
