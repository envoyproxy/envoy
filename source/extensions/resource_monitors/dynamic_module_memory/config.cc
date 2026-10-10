#include "source/extensions/resource_monitors/dynamic_module_memory/config.h"

#include "envoy/registry/registry.h"

#include "source/extensions/resource_monitors/dynamic_module_memory/dynamic_module_memory_monitor.h"

namespace Envoy {
namespace Extensions {
namespace ResourceMonitors {
namespace DynamicModuleMemory {

absl::StatusOr<Server::ResourceMonitorPtr>
DynamicModuleMemoryMonitorFactory::createResourceMonitorFromProtoTyped(
    const envoy::extensions::resource_monitors::dynamic_module_memory::v3::
        DynamicModuleMemoryConfig& config,
    Server::Configuration::ResourceMonitorFactoryContext&) {
  return std::make_unique<DynamicModuleMemoryMonitor>(config, DynamicModules::ModuleMemoryReader());
}

/**
 * Static registration for the dynamic module memory resource monitor factory. @see RegistryFactory.
 */
REGISTER_FACTORY(DynamicModuleMemoryMonitorFactory, Server::Configuration::ResourceMonitorFactory);

} // namespace DynamicModuleMemory
} // namespace ResourceMonitors
} // namespace Extensions
} // namespace Envoy
