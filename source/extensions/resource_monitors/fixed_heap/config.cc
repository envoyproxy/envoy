#include "extensions/resource_monitors/fixed_heap/config.h"

#include "envoy/registry/registry.h"

#include "common/protobuf/utility.h"

#include "extensions/resource_monitors/fixed_heap/fixed_heap_monitor.h"

namespace Envoy {
namespace Extensions {
namespace ResourceMonitors {
namespace FixedHeapMonitor {

Server::ResourceMonitorPtr FixedHeapMonitorFactory::createResourceMonitor(
    const Protobuf::Message& config,
    Server::Configuration::ResourceMonitorFactoryContext& /*unused_context*/) {
  return std::make_unique<FixedHeapMonitor>(
      MessageUtil::downcastAndValidate<
          const envoy::config::resource_monitor::fixed_heap::v2alpha::FixedHeapConfig&>(config));
}

/**
 * Static registration for the fixed heap resource monitor factory. @see RegistryFactory.
 */
static Registry::RegisterFactory<FixedHeapMonitorFactory,
                                 Server::Configuration::ResourceMonitorFactory>
    registered_;

} // namespace FixedHeapMonitor
} // namespace ResourceMonitors
} // namespace Extensions
} // namespace Envoy
