#pragma once

#include "envoy/extensions/resource_monitors/cgroup_memory/v3/cgroup_memory.pb.h"
#include "envoy/extensions/resource_monitors/cgroup_memory/v3/cgroup_memory.pb.validate.h"
#include "envoy/server/resource_monitor_config.h"

#include "source/extensions/resource_monitors/common/factory_base.h"

namespace Envoy {
namespace Extensions {
namespace ResourceMonitors {
namespace CgroupMemory {

/**
 * Config registration for the cgroup memory resource monitor.
 * @see RegistryFactory.
 */
class CgroupMemoryMonitorFactory
    : public Common::FactoryBase<
          envoy::extensions::resource_monitors::cgroup_memory::v3::CgroupMemoryConfig> {
public:
  CgroupMemoryMonitorFactory() : FactoryBase("envoy.resource_monitors.cgroup_memory") {}

private:
  // Common::FactoryBase
  Server::ResourceMonitorPtr createResourceMonitorFromProtoTyped(
      const envoy::extensions::resource_monitors::cgroup_memory::v3::CgroupMemoryConfig& config,
      Server::Configuration::ResourceMonitorFactoryContext& context) override;
};

} // namespace CgroupMemory
} // namespace ResourceMonitors
} // namespace Extensions
} // namespace Envoy
