#pragma once

#include "envoy/extensions/resource_monitors/fixed_heap/v3/fixed_heap.pb.h"
#include "envoy/extensions/resource_monitors/fixed_heap/v3/fixed_heap.pb.validate.h"
#include "envoy/server/resource_monitor_config.h"

#include "source/extensions/resource_monitors/common/factory_base.h"

namespace Envoy {
namespace Extensions {
namespace ResourceMonitors {
namespace FixedHeapMonitor {

class FixedHeapMonitorFactory
    : public Common::FactoryBase<
          envoy::extensions::resource_monitors::fixed_heap::v3::FixedHeapConfig> {
public:
  FixedHeapMonitorFactory() : FactoryBase("envoy.resource_monitors.fixed_heap") {}

private:
  Server::ResourceMonitorPtr createResourceMonitorFromProtoTyped(
      const envoy::extensions::resource_monitors::fixed_heap::v3::FixedHeapConfig& config,
      Server::Configuration::ResourceMonitorFactoryContext& context) override;
};

} // namespace FixedHeapMonitor
} // namespace ResourceMonitors
} // namespace Extensions
} // namespace Envoy
