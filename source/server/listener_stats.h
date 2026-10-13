#pragma once

#include "envoy/stats/scope.h"
#include "envoy/stats/stats_macros.h"

namespace Envoy {
namespace Server {

// This macro defines the listener stats which each Envoy listener will have.
#define ALL_LISTENER_STATS(COUNTER, GAUGE, HISTOGRAM)                                              \
  COUNTER(downstream_cx_destroy)                                                                   \
  COUNTER(downstream_cx_overflow)                                                                  \
  COUNTER(downstream_cx_total)                                                                     \
  COUNTER(downstream_cx_transport_socket_connect_timeout)                                          \
  COUNTER(downstream_cx_overload_reject)                                                           \
  COUNTER(downstream_global_cx_overflow)                                                           \
  COUNTER(downstream_pre_cx_timeout)                                                               \
  COUNTER(downstream_listener_filter_remote_close)                                                 \
  COUNTER(downstream_listener_filter_error)                                                        \
  COUNTER(no_filter_chain_match)                                                                   \
  GAUGE(downstream_cx_active, Accumulate)                                                          \
  GAUGE(downstream_pre_cx_active, Accumulate)                                                      \
  HISTOGRAM(downstream_cx_length_ms, Milliseconds)                                                 \
  HISTOGRAM(connections_accepted_per_socket_event, Unspecified)

/**
 * Wrapper struct for listener stats. @see stats_macros.h
 */
struct ListenerStats {
  ALL_LISTENER_STATS(GENERATE_COUNTER_STRUCT, GENERATE_GAUGE_STRUCT, GENERATE_HISTOGRAM_STRUCT)
};

// This macro defines the listener stats which each Envoy worker will have.
#define ALL_PER_HANDLER_LISTENER_STATS(COUNTER, GAUGE)                                             \
  COUNTER(downstream_cx_total)                                                                     \
  GAUGE(downstream_cx_active, Accumulate)

/**
 * Wrapper struct for per-handler listener stats. @see stats_macros.h
 */
struct PerHandlerListenerStats {
  ALL_PER_HANDLER_LISTENER_STATS(GENERATE_COUNTER_STRUCT, GENERATE_GAUGE_STRUCT)
};

} // namespace Server
} // namespace Envoy
