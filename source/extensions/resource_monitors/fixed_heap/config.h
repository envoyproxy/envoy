#pragma once

#include "envoy/config/resource_monitor/fixed_heap/v2alpha/fixed_heap.pb.validate.h"
#include "envoy/server/resource_monitor_config.h"

#include "extensions/resource_monitors/common/factory_base.h"
#include "extensions/resource_monitors/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace ResourceMonitors {
namespace FixedHeapMonitor {

class FixedHeapMonitorFactory
    : public Common::FactoryBase<
          envoy::config::resource_monitor::fixed_heap::v2alpha::FixedHeapConfig> {
public:
  FixedHeapMonitorFactory()
      : FactoryBase(ResourceMonitorNames::get().FIXED_HEAP_RESOURCE_MONITOR) {}

private:
  Server::ResourceMonitorPtr createResourceMonitorFromProtoTyped(
      const envoy::config::resource_monitor::fixed_heap::v2alpha::FixedHeapConfig& config,
      Server::Configuration::ResourceMonitorFactoryContext& context) override;
};

} // namespace FixedHeapMonitor
} // namespace ResourceMonitors
} // namespace Extensions
} // namespace Envoy
