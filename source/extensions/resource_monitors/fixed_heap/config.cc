#include "source/extensions/resource_monitors/fixed_heap/config.h"

#include "envoy/common/exception.h"
#include "envoy/extensions/resource_monitors/fixed_heap/v3/fixed_heap.pb.h"
#include "envoy/extensions/resource_monitors/fixed_heap/v3/fixed_heap.pb.validate.h"
#include "envoy/registry/registry.h"

#include "source/common/common/utility.h"
#include "source/common/runtime/runtime_protos.h"
#include "source/extensions/resource_monitors/fixed_heap/fixed_heap_monitor.h"

namespace Envoy {
namespace Extensions {
namespace ResourceMonitors {
namespace FixedHeapMonitor {

Server::ResourceMonitorPtr FixedHeapMonitorFactory::createResourceMonitorFromProtoTyped(
    const envoy::extensions::resource_monitors::fixed_heap::v3::FixedHeapConfig& config,
    Server::Configuration::ResourceMonitorFactoryContext& context) {
  if ((config.max_heap_size_bytes() > 0) && config.has_max_heap_size_bytes_runtime()) {
    throw EnvoyException("fixed_heap: exactly one of max_heap_size_bytes or "
                         "max_heap_size_bytes_runtime must be set");
  }

  if (config.has_max_heap_size_bytes_runtime()) {
    Runtime::UInt64 runtime_uint64(config.max_heap_size_bytes_runtime(), context.runtime());
    if (runtime_uint64.value() == 0) {
      throw EnvoyException("fixed_heap: max heap size must be greater than 0");
    }
    return std::make_unique<FixedHeapMonitor>(
        absl::variant<uint64_t, Runtime::UInt64>(std::move(runtime_uint64)));
  } else {
    if (config.max_heap_size_bytes() == 0) {
      throw EnvoyException("fixed_heap: max heap size must be greater than 0");
    }
    return std::make_unique<FixedHeapMonitor>(
        absl::variant<uint64_t, Runtime::UInt64>(config.max_heap_size_bytes()));
  }
}

/**
 * Static registration for the fixed heap resource monitor factory. @see RegistryFactory.
 */
REGISTER_FACTORY(FixedHeapMonitorFactory, Server::Configuration::ResourceMonitorFactory);

} // namespace FixedHeapMonitor
} // namespace ResourceMonitors
} // namespace Extensions
} // namespace Envoy
