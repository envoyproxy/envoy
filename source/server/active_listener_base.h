#pragma once

#include "envoy/network/connection_handler.h"
#include "envoy/network/listener.h"
#include "envoy/stats/scope.h"

#include "source/server/listener_stats.h"

namespace Envoy {
namespace Server {

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
