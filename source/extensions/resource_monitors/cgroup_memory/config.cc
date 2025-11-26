#include "source/extensions/resource_monitors/cgroup_memory/config.h"

#include "envoy/extensions/resource_monitors/cgroup_memory/v3/cgroup_memory.pb.h"
#include "envoy/extensions/resource_monitors/cgroup_memory/v3/cgroup_memory.pb.validate.h"
#include "envoy/registry/registry.h"

#include "source/common/protobuf/utility.h"
#include "source/extensions/resource_monitors/cgroup_memory/cgroup_memory_monitor.h"

namespace Envoy {
namespace Extensions {
namespace ResourceMonitors {
namespace CgroupMemory {

Server::ResourceMonitorPtr CgroupMemoryMonitorFactory::createResourceMonitorFromProtoTyped(
    const envoy::extensions::resource_monitors::cgroup_memory::v3::CgroupMemoryConfig& config,
    Server::Configuration::ResourceMonitorFactoryContext& context) {
  return std::make_unique<CgroupMemoryMonitor>(config, context.api().fileSystem());
}

/**
 * Static registration for the cgroup memory monitor factory. @see RegistryFactory.
 */
REGISTER_FACTORY(CgroupMemoryMonitorFactory, Server::Configuration::ResourceMonitorFactory);

} // namespace CgroupMemory
} // namespace ResourceMonitors
} // namespace Extensions
} // namespace Envoy
