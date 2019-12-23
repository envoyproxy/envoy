#include "extensions/resource_monitors/unused_heap/config.h"

#include "envoy/config/resource_monitor/unused_heap/v2alpha/unused_heap.pb.h"
#include "envoy/config/resource_monitor/unused_heap/v2alpha/unused_heap.pb.validate.h"
#include "envoy/registry/registry.h"

#include "common/protobuf/utility.h"

#include "extensions/resource_monitors/unused_heap/unused_heap_monitor.h"

namespace Envoy {
namespace Extensions {
namespace ResourceMonitors {
namespace UnusedHeapMonitor {

Server::ResourceMonitorPtr UnusedHeapMonitorFactory::createResourceMonitorFromProtoTyped(
    const envoy::config::resource_monitor::unused_heap::v2alpha::UnusedHeapConfig& config,
    Server::Configuration::ResourceMonitorFactoryContext& /*unused_context*/) {
  return std::make_unique<UnusedHeapMonitor>(config);
}

/**
 * Static registration for the unused heap resource monitor factory. @see RegistryFactory.
 */
REGISTER_FACTORY(UnusedHeapMonitorFactory, Server::Configuration::ResourceMonitorFactory);

} // namespace UnusedHeapMonitor
} // namespace ResourceMonitors
} // namespace Extensions
} // namespace Envoy
