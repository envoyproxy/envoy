#include "source/extensions/resource_monitors/dynamic_module_memory/dynamic_module_memory_monitor.h"

#include "source/common/common/logger.h"

namespace Envoy {
namespace Extensions {
namespace ResourceMonitors {
namespace DynamicModuleMemory {

DynamicModuleMemoryMonitor::DynamicModuleMemoryMonitor(
    const envoy::extensions::resource_monitors::dynamic_module_memory::v3::
        DynamicModuleMemoryConfig& config,
    DynamicModules::ModuleMemoryReader reader)
    : module_name_(config.module_name()), max_heap_size_bytes_(config.max_heap_size_bytes()),
      reader_(std::move(reader)) {}

void DynamicModuleMemoryMonitor::updateResourceUsage(Server::ResourceUpdateCallbacks& callbacks) {
  const absl::StatusOr<uint64_t> allocated_bytes = reader_.allocatedBytes(module_name_);
  if (!allocated_bytes.ok()) {
    callbacks.onFailure(allocated_bytes.status());
    return;
  }

  Server::ResourceUsage usage;
  usage.resource_pressure_ = *allocated_bytes / static_cast<double>(max_heap_size_bytes_);

  ENVOY_LOG_MISC(trace, "DynamicModuleMemoryMonitor: module={}, allocated={}, max_heap={}",
                 module_name_, *allocated_bytes, max_heap_size_bytes_);

  callbacks.onSuccess(usage);
}

} // namespace DynamicModuleMemory
} // namespace ResourceMonitors
} // namespace Extensions
} // namespace Envoy
