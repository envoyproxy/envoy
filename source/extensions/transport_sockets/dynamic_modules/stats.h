#pragma once

#include "envoy/stats/scope.h"
#include "envoy/stats/stats_macros.h"

namespace Envoy {
namespace Extensions {
namespace DynamicModules {
namespace TransportSockets {

/**
 * All stats for dynamic module transport sockets. @see stats_macros.h
 */
#define ALL_DYNAMIC_MODULE_TRANSPORT_SOCKET_STATS(COUNTER)                                         \
  COUNTER(socket_created)                                                                          \
  COUNTER(handshake_complete)                                                                      \
  COUNTER(connection_error)                                                                        \
  COUNTER(read_error)                                                                              \
  COUNTER(write_error)                                                                             \
  COUNTER(module_init_failed)

/**
 * Wrapper struct for dynamic module transport socket stats. @see stats_macros.h
 */
struct DynamicModuleTransportSocketStats {
  ALL_DYNAMIC_MODULE_TRANSPORT_SOCKET_STATS(GENERATE_COUNTER_STRUCT)
};

/**
 * Generate stats for dynamic module transport sockets.
 * @param store is the stats scope to use for generation.
 * @return the generated stats structure.
 */
DynamicModuleTransportSocketStats generateDynamicModuleTransportSocketStats(Stats::Scope& store);

} // namespace TransportSockets
} // namespace DynamicModules
} // namespace Extensions
} // namespace Envoy
