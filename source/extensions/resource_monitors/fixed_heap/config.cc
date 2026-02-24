#include "source/extensions/resource_monitors/fixed_heap/config.h"

#include "envoy/common/exception.h"
#include "envoy/extensions/resource_monitors/fixed_heap/v3/fixed_heap.pb.h"
#include "envoy/extensions/resource_monitors/fixed_heap/v3/fixed_heap.pb.validate.h"
#include "envoy/registry/registry.h"

#include "source/common/common/fmt.h"
#include "source/common/common/utility.h"
#include "source/common/config/datasource.h"
#include "source/extensions/resource_monitors/fixed_heap/fixed_heap_monitor.h"

namespace Envoy {
namespace Extensions {
namespace ResourceMonitors {
namespace FixedHeapMonitor {

Server::ResourceMonitorPtr FixedHeapMonitorFactory::createResourceMonitorFromProtoTyped(
    const envoy::extensions::resource_monitors::fixed_heap::v3::FixedHeapConfig& config,
    Server::Configuration::ResourceMonitorFactoryContext& context) {
  if ((config.max_heap_size_bytes() != 0) == config.has_max_heap_size_bytes_source()) {
    throw EnvoyException(
        "fixed_heap: exactly one of max_heap_size_bytes or max_heap_size_bytes_source must be set");
  }

  uint64_t max_heap;
  if (config.has_max_heap_size_bytes_source()) {
    auto source_or =
        Config::DataSource::read(config.max_heap_size_bytes_source(), false, context.api());
    THROW_IF_NOT_OK(source_or.status());
    const std::string trimmed(std::string(StringUtil::trim(*source_or)));
    if (!absl::SimpleAtoi(trimmed, &max_heap)) {
      throw EnvoyException(fmt::format(
          "fixed_heap max_heap_size_bytes_source must be a positive integer, got '{}'", trimmed));
    }
  } else {
    max_heap = config.max_heap_size_bytes();
  }

  if (max_heap == 0) {
    throw EnvoyException("fixed_heap: max heap size must be greater than 0");
  }
  return std::make_unique<FixedHeapMonitor>(max_heap);
}

/**
 * Static registration for the fixed heap resource monitor factory. @see RegistryFactory.
 */
REGISTER_FACTORY(FixedHeapMonitorFactory, Server::Configuration::ResourceMonitorFactory);

} // namespace FixedHeapMonitor
} // namespace ResourceMonitors
} // namespace Extensions
} // namespace Envoy
