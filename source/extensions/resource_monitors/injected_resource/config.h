#pragma once

#include "envoy/config/resource_monitor/injected_resource/v2alpha/injected_resource.pb.h"
#include "envoy/config/resource_monitor/injected_resource/v2alpha/injected_resource.pb.validate.h"
#include "envoy/server/resource_monitor_config.h"

#include "extensions/resource_monitors/common/factory_base.h"
#include "extensions/resource_monitors/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace ResourceMonitors {
namespace InjectedResourceMonitor {

class InjectedResourceMonitorFactory
    : public Common::FactoryBase<
          envoy::config::resource_monitor::injected_resource::v2alpha::InjectedResourceConfig> {
public:
  InjectedResourceMonitorFactory() : FactoryBase(ResourceMonitorNames::get().InjectedResource) {}

private:
  Server::ResourceMonitorPtr createResourceMonitorFromProtoTyped(
      const envoy::config::resource_monitor::injected_resource::v2alpha::InjectedResourceConfig&
          config,
      Server::Configuration::ResourceMonitorFactoryContext& context) override;
};

} // namespace InjectedResourceMonitor
} // namespace ResourceMonitors
} // namespace Extensions
} // namespace Envoy
