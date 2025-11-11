#include "source/extensions/transport_sockets/dynamic_modules/stats.h"

#include "envoy/stats/scope.h"

namespace Envoy {
namespace Extensions {
namespace DynamicModules {
namespace TransportSockets {

DynamicModuleTransportSocketStats generateDynamicModuleTransportSocketStats(Stats::Scope& store) {
  const std::string prefix = "dynamic_module_transport_socket.";
  return {ALL_DYNAMIC_MODULE_TRANSPORT_SOCKET_STATS(POOL_COUNTER_PREFIX(store, prefix))};
}

} // namespace TransportSockets
} // namespace DynamicModules
} // namespace Extensions
} // namespace Envoy
