#pragma once

#include "envoy/network/connection_handler.h"
#include "envoy/network/listener.h"
#include "envoy/stats/scope.h"

namespace Envoy {
namespace Server {

#define ALL_LISTENER_STATS(COUNTER, GAUGE, HISTOGRAM)                                              \
  COUNTER(downstream_cx_destroy)                                                                   \
  COUNTER(downstream_cx_overflow)                                                                  \
  COUNTER(downstream_cx_total)                                                                     \
  COUNTER(downstream_cx_transport_socket_connect_timeout)                                          \
  COUNTER(downstream_cx_overload_reject)                                                           \
  COUNTER(downstream_global_cx_overflow)                                                           \
  COUNTER(downstream_pre_cx_timeout)                                                               \
  COUNTER(no_filter_chain_match)                                                                   \
  GAUGE(downstream_cx_active, Accumulate)                                                          \
  GAUGE(downstream_pre_cx_active, Accumulate)                                                      \
  HISTOGRAM(downstream_cx_length_ms, Milliseconds)

/**
 * Wrapper struct for listener stats. @see stats_macros.h
 */
struct ListenerStats {
  ALL_LISTENER_STATS(GENERATE_COUNTER_STRUCT, GENERATE_GAUGE_STRUCT, GENERATE_HISTOGRAM_STRUCT)
};

#define ALL_PER_HANDLER_LISTENER_STATS(COUNTER, GAUGE)                                             \
  COUNTER(downstream_cx_total)                                                                     \
  GAUGE(downstream_cx_active, Accumulate)

/**
 * Wrapper struct for per-handler listener stats. @see stats_macros.h
 */
struct PerHandlerListenerStats {
  ALL_PER_HANDLER_LISTENER_STATS(GENERATE_COUNTER_STRUCT, GENERATE_GAUGE_STRUCT)
};

/**
 * Wrapper for an active listener owned by this handler.
 */
class ActiveListenerImplBase : public virtual Network::ConnectionHandler::ActiveListener {
public:
  ActiveListenerImplBase(Network::ConnectionHandler& parent, Network::ListenerConfig* config)
      : stats_({ALL_LISTENER_STATS(POOL_COUNTER(config->listenerScope()),
                                   POOL_GAUGE(config->listenerScope()),
                                   POOL_HISTOGRAM(config->listenerScope()))}),
        per_worker_stats_({ALL_PER_HANDLER_LISTENER_STATS(
            POOL_COUNTER_PREFIX(config->listenerScope(), parent.statPrefix()),
            POOL_GAUGE_PREFIX(config->listenerScope(), parent.statPrefix()))}),
        config_(config) {}

  // Network::ConnectionHandler::ActiveListener.
  uint64_t listenerTag() override { return config_->listenerTag(); }

  ListenerStats stats_;
  PerHandlerListenerStats per_worker_stats_;
  Network::ListenerConfig* config_{};
};

} // namespace Server
} // namespace Envoy
