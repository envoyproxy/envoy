#include "extensions/resource_monitors/fixed_heap/config.h"

#include "envoy/config/resource_monitor/fixed_heap/v2alpha/fixed_heap.pb.h"
#include "envoy/config/resource_monitor/fixed_heap/v2alpha/fixed_heap.pb.validate.h"
#include "envoy/registry/registry.h"

#include "common/protobuf/utility.h"

#include "extensions/resource_monitors/fixed_heap/fixed_heap_monitor.h"

namespace Envoy {
namespace Extensions {
namespace ResourceMonitors {
namespace FixedHeapMonitor {

Server::ResourceMonitorPtr FixedHeapMonitorFactory::createResourceMonitorFromProtoTyped(
    const envoy::config::resource_monitor::fixed_heap::v2alpha::FixedHeapConfig& config,
    Server::Configuration::ResourceMonitorFactoryContext& /*unused_context*/) {
  return std::make_unique<FixedHeapMonitor>(config);
}

/**
 * Static registration for the fixed heap resource monitor factory. @see RegistryFactory.
 */
REGISTER_FACTORY(FixedHeapMonitorFactory, Server::Configuration::ResourceMonitorFactory);

} // namespace FixedHeapMonitor
} // namespace ResourceMonitors
} // namespace Extensions
} // namespace Envoy
