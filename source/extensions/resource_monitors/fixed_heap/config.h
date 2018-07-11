#pragma once

#include "envoy/server/resource_monitor_config.h"

#include "extensions/resource_monitors/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace ResourceMonitors {
namespace FixedHeapMonitor {

class FixedHeapMonitorFactory : public Server::Configuration::ResourceMonitorFactory {
public:
  Server::ResourceMonitorPtr
  createResourceMonitor(const Protobuf::Message& config,
                        Server::Configuration::ResourceMonitorFactoryContext& context) override;

  std::string name() override { return ResourceMonitorNames::get().FIXED_HEAP_RESOURCE_MONITOR; }
};

} // namespace FixedHeapMonitor
} // namespace ResourceMonitors
} // namespace Extensions
} // namespace Envoy
