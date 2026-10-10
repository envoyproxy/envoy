#pragma once

#include <cstdint>
#include <string>

#include "envoy/extensions/resource_monitors/dynamic_module_memory/v3/dynamic_module_memory.pb.h"
#include "envoy/server/resource_monitor.h"

#include "source/extensions/dynamic_modules/module_memory.h"

namespace Envoy {
namespace Extensions {
namespace ResourceMonitors {
namespace DynamicModuleMemory {

/**
 * Reports the heap memory allocated by a dynamic module as a fraction of a configured heap size.
 */
class DynamicModuleMemoryMonitor : public Server::ResourceMonitor {
public:
  DynamicModuleMemoryMonitor(const envoy::extensions::resource_monitors::dynamic_module_memory::v3::
                                 DynamicModuleMemoryConfig& config,
                             DynamicModules::ModuleMemoryReader reader);

  // Server::ResourceMonitor
  void updateResourceUsage(Server::ResourceUpdateCallbacks& callbacks) override;

private:
  const std::string module_name_;
  const uint64_t max_heap_size_bytes_;
  DynamicModules::ModuleMemoryReader reader_;
};

} // namespace DynamicModuleMemory
} // namespace ResourceMonitors
} // namespace Extensions
} // namespace Envoy
