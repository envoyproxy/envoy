#include "extensions/resource_monitors/injected_resource/config.h"

#include "envoy/registry/registry.h"

#include "common/protobuf/utility.h"

#include "extensions/resource_monitors/injected_resource/injected_resource_monitor.h"

namespace Envoy {
namespace Extensions {
namespace ResourceMonitors {
namespace InjectedResourceMonitor {

Server::ResourceMonitorPtr InjectedResourceMonitorFactory::createResourceMonitorFromProtoTyped(
    const envoy::config::resource_monitor::injected_resource::v2alpha::InjectedResourceConfig&
        config,
    Server::Configuration::ResourceMonitorFactoryContext& context) {
  return std::make_unique<InjectedResourceMonitor>(config, context);
}

/**
 * Static registration for the injected resource monitor factory. @see RegistryFactory.
 */
static Registry::RegisterFactory<InjectedResourceMonitorFactory,
                                 Server::Configuration::ResourceMonitorFactory>
    registered_;

} // namespace InjectedResourceMonitor
} // namespace ResourceMonitors
} // namespace Extensions
} // namespace Envoy
