#pragma once

#include "envoy/extensions/resource_monitors/dynamic_modules/v3/dynamic_modules.pb.h"
#include "envoy/extensions/resource_monitors/dynamic_modules/v3/dynamic_modules.pb.validate.h"
#include "envoy/server/resource_monitor_config.h"

#include "source/extensions/resource_monitors/common/factory_base.h"

namespace Envoy {
namespace Extensions {
namespace ResourceMonitors {
namespace DynamicModules {

class DynamicModuleResourceMonitorFactory
    : public Common::FactoryBase<
          envoy::extensions::resource_monitors::dynamic_modules::v3::DynamicModuleResourceMonitor> {
public:
  DynamicModuleResourceMonitorFactory() : FactoryBase("envoy.resource_monitors.dynamic_modules") {}

private:
  absl::StatusOr<Server::ResourceMonitorPtr> createResourceMonitorFromProtoTyped(
      const envoy::extensions::resource_monitors::dynamic_modules::v3::DynamicModuleResourceMonitor&
          config,
      Server::Configuration::ResourceMonitorFactoryContext& context) override;
};

} // namespace DynamicModules
} // namespace ResourceMonitors
} // namespace Extensions
} // namespace Envoy
