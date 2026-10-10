#pragma once

#include "envoy/extensions/resource_monitors/dynamic_module_memory/v3/dynamic_module_memory.pb.h"
#include "envoy/extensions/resource_monitors/dynamic_module_memory/v3/dynamic_module_memory.pb.validate.h"
#include "envoy/server/resource_monitor_config.h"

#include "source/extensions/resource_monitors/common/factory_base.h"

namespace Envoy {
namespace Extensions {
namespace ResourceMonitors {
namespace DynamicModuleMemory {

class DynamicModuleMemoryMonitorFactory
    : public Common::FactoryBase<envoy::extensions::resource_monitors::dynamic_module_memory::v3::
                                     DynamicModuleMemoryConfig> {
public:
  DynamicModuleMemoryMonitorFactory()
      : FactoryBase("envoy.resource_monitors.dynamic_module_memory") {}

private:
  absl::StatusOr<Server::ResourceMonitorPtr> createResourceMonitorFromProtoTyped(
      const envoy::extensions::resource_monitors::dynamic_module_memory::v3::
          DynamicModuleMemoryConfig& config,
      Server::Configuration::ResourceMonitorFactoryContext& context) override;
};

} // namespace DynamicModuleMemory
} // namespace ResourceMonitors
} // namespace Extensions
} // namespace Envoy
