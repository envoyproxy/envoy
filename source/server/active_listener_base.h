#pragma once

#include "envoy/network/connection_handler.h"
#include "envoy/network/listener.h"
#include "envoy/stats/scope.h"

#include "source/common/stats/prefix_utility.h"
#include "source/common/stats/utility.h"
#include "source/server/listener_stats.h"

#include "absl/strings/strip.h"

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
        per_worker_stats_(generatePerWorkerStats(parent, config->listenerScope())),
        config_(config) {}

  // Network::ConnectionHandler::ActiveListener.
  uint64_t listenerTag() override { return config_->listenerTag(); }

  ListenerStats stats_;
  PerHandlerListenerStats per_worker_stats_;
  Network::ListenerConfig* config_{};

private:
  static PerHandlerListenerStats generatePerWorkerStats(Network::ConnectionHandler& parent,
                                                        Stats::Scope& scope) {
    // listener.<address>.(worker_<id>.)*: the handler's stat prefix is its dispatcher's name
    // with a trailing dot (strip it), and a worker handler contributes the envoy.worker_id tag.
    // TODO(wbpcode): Consider to expose the dispatcher name directly from the parent connection
    // handler.
    const auto prefix = Stats::workerStatPrefix(scope.symbolTable(), {},
                                                absl::StripSuffix(parent.statPrefix(), "."));
    Stats::LiteScopeHelper helper(scope, prefix.baseName(), prefix.tags(), prefix.name());
    return {ALL_PER_HANDLER_LISTENER_STATS(POOL_COUNTER(helper), POOL_GAUGE(helper))};
  }
};

} // namespace Server
} // namespace Envoy
