#include "extensions/resource_monitors/downstream_connections/config.h"

#include "envoy/config/resource_monitor/downstream_connections/v3/downstream_connections.pb.h"
#include "envoy/config/resource_monitor/downstream_connections/v3/downstream_connections.pb.validate.h"
#include "envoy/registry/registry.h"

#include "common/protobuf/utility.h"

#include "extensions/resource_monitors/downstream_connections/downstream_connections_monitor.h"

namespace Envoy {
namespace Extensions {
namespace ResourceMonitors {
namespace DownstreamConnectionsMonitor {

Server::ResourceMonitorPtr DownstreamConnectionsMonitorFactory::createResourceMonitorFromProtoTyped(
    const envoy::config::resource_monitor::downstream_connections::v3::DownstreamConnectionsConfig&
        config,
    Server::Configuration::ResourceMonitorFactoryContext& /*unused_context*/) {
  return std::make_unique<DownstreamConnectionsMonitor>(config);
}

/**
 * Static registration for the downstream connections resource monitor factory. @see
 * RegistryFactory.
 */
REGISTER_FACTORY(DownstreamConnectionsMonitorFactory,
                 Server::Configuration::ResourceMonitorFactory);

} // namespace DownstreamConnectionsMonitor
} // namespace ResourceMonitors
} // namespace Extensions
} // namespace Envoy
