#pragma once

#include "envoy/extensions/resource_monitors/injected_resource/v3/injected_resource.pb.h"
#include "envoy/extensions/resource_monitors/injected_resource/v3/injected_resource.pb.validate.h"
#include "envoy/server/resource_monitor_config.h"

#include "source/extensions/resource_monitors/common/factory_base.h"

namespace Envoy {
namespace Extensions {
namespace ResourceMonitors {
namespace InjectedResourceMonitor {

class InjectedResourceMonitorFactory
    : public Common::FactoryBase<
          envoy::extensions::resource_monitors::injected_resource::v3::InjectedResourceConfig> {
public:
  InjectedResourceMonitorFactory() : FactoryBase("envoy.resource_monitors.injected_resource") {}

private:
  Server::ResourceMonitorPtr createResourceMonitorFromProtoTyped(
      const envoy::extensions::resource_monitors::injected_resource::v3::InjectedResourceConfig&
          config,
      Server::Configuration::ResourceMonitorFactoryContext& context) override;
};

} // namespace InjectedResourceMonitor
} // namespace ResourceMonitors
} // namespace Extensions
} // namespace Envoy
