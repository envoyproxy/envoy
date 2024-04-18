#include "source/extensions/resource_monitors/downstream_connections/config.h"

#include "envoy/extensions/resource_monitors/downstream_connections/v3/downstream_connections.pb.h"
#include "envoy/extensions/resource_monitors/downstream_connections/v3/downstream_connections.pb.validate.h"
#include "envoy/registry/registry.h"

#include "source/common/protobuf/utility.h"
#include "source/extensions/resource_monitors/downstream_connections/downstream_connections_monitor.h"

namespace Envoy {
namespace Extensions {
namespace ResourceMonitors {
namespace DownstreamConnections {

Server::ProactiveResourceMonitorPtr
ActiveDownstreamConnectionsMonitorFactory::createProactiveResourceMonitorFromProtoTyped(
    const envoy::extensions::resource_monitors::downstream_connections::v3::
        DownstreamConnectionsConfig& config,
    Server::Configuration::ResourceMonitorFactoryContext& /*unused_context*/) {
  return std::make_unique<ActiveDownstreamConnectionsResourceMonitor>(config);
}

/**
 * Static registration for the downstream connections resource monitor factory. @see
 * RegistryFactory.
 */
REGISTER_FACTORY(ActiveDownstreamConnectionsMonitorFactory,
                 Server::Configuration::ProactiveResourceMonitorFactory);

} // namespace DownstreamConnections
} // namespace ResourceMonitors
} // namespace Extensions
} // namespace Envoy
