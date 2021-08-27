#include "source/extensions/resource_monitors/injected_resource/config.h"

#include "envoy/extensions/resource_monitors/injected_resource/v3/injected_resource.pb.h"
#include "envoy/extensions/resource_monitors/injected_resource/v3/injected_resource.pb.validate.h"
#include "envoy/registry/registry.h"

#include "source/common/protobuf/utility.h"
#include "source/extensions/resource_monitors/injected_resource/injected_resource_monitor.h"

namespace Envoy {
namespace Extensions {
namespace ResourceMonitors {
namespace InjectedResourceMonitor {

Server::ResourceMonitorPtr InjectedResourceMonitorFactory::createResourceMonitorFromProtoTyped(
    const envoy::extensions::resource_monitors::injected_resource::v3::InjectedResourceConfig&
        config,
    Server::Configuration::ResourceMonitorFactoryContext& context) {
  return std::make_unique<InjectedResourceMonitor>(config, context);
}

/**
 * Static registration for the injected resource monitor factory. @see RegistryFactory.
 */
REGISTER_FACTORY(InjectedResourceMonitorFactory, Server::Configuration::ResourceMonitorFactory);

} // namespace InjectedResourceMonitor
} // namespace ResourceMonitors
} // namespace Extensions
} // namespace Envoy
