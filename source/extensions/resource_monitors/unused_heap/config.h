#pragma once

#include "envoy/config/resource_monitor/unused_heap/v2alpha/unused_heap.pb.h"
#include "envoy/config/resource_monitor/unused_heap/v2alpha/unused_heap.pb.validate.h"
#include "envoy/server/resource_monitor_config.h"

#include "extensions/resource_monitors/common/factory_base.h"
#include "extensions/resource_monitors/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace ResourceMonitors {
namespace UnusedHeapMonitor {

class UnusedHeapMonitorFactory
    : public Common::FactoryBase<
          envoy::config::resource_monitor::unused_heap::v2alpha::UnusedHeapConfig> {
public:
  UnusedHeapMonitorFactory() : FactoryBase(ResourceMonitorNames::get().UnusedHeap) {}

private:
  Server::ResourceMonitorPtr createResourceMonitorFromProtoTyped(
      const envoy::config::resource_monitor::unused_heap::v2alpha::UnusedHeapConfig& config,
      Server::Configuration::ResourceMonitorFactoryContext& context) override;
};

} // namespace UnusedHeapMonitor
} // namespace ResourceMonitors
} // namespace Extensions
} // namespace Envoy
