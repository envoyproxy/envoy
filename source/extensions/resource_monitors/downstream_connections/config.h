#pragma once

#include "envoy/config/resource_monitor/downstream_connections/v3/downstream_connections.pb.h"
#include "envoy/config/resource_monitor/downstream_connections/v3/downstream_connections.pb.validate.h"
#include "envoy/server/resource_monitor_config.h"

#include "extensions/resource_monitors/common/factory_base.h"
#include "extensions/resource_monitors/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace ResourceMonitors {
namespace DownstreamConnectionsMonitor {

class DownstreamConnectionsMonitorFactory
    : public Common::FactoryBase<envoy::config::resource_monitor::downstream_connections::v3::
                                     DownstreamConnectionsConfig> {
public:
  DownstreamConnectionsMonitorFactory()
      : FactoryBase(ResourceMonitorNames::get().DownstreamConnections) {}

private:
  Server::ResourceMonitorPtr createResourceMonitorFromProtoTyped(
      const envoy::config::resource_monitor::downstream_connections::v3::
          DownstreamConnectionsConfig& config,
      Server::Configuration::ResourceMonitorFactoryContext& context) override;
};

} // namespace DownstreamConnectionsMonitor
} // namespace ResourceMonitors
} // namespace Extensions
} // namespace Envoy
