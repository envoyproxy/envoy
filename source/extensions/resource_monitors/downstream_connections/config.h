#pragma once

#include "envoy/extensions/resource_monitors/downstream_connections/v3/downstream_connections.pb.h"
#include "envoy/extensions/resource_monitors/downstream_connections/v3/downstream_connections.pb.validate.h"
#include "envoy/server/resource_monitor_config.h"

#include "source/extensions/resource_monitors/common/factory_base.h"

namespace Envoy {
namespace Extensions {
namespace ResourceMonitors {
namespace DownstreamConnections {

class ActiveDownstreamConnectionsMonitorFactory
    : public Common::ProactiveFactoryBase<
          envoy::extensions::resource_monitors::downstream_connections::v3::
              DownstreamConnectionsConfig> {
public:
  ActiveDownstreamConnectionsMonitorFactory()
      : ProactiveFactoryBase("envoy.resource_monitors.global_downstream_max_connections") {}

private:
  Server::ProactiveResourceMonitorPtr createProactiveResourceMonitorFromProtoTyped(
      const envoy::extensions::resource_monitors::downstream_connections::v3::
          DownstreamConnectionsConfig& config,
      Server::Configuration::ResourceMonitorFactoryContext& context) override;
};

} // namespace DownstreamConnections
} // namespace ResourceMonitors
} // namespace Extensions
} // namespace Envoy
