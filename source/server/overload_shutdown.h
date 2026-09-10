#pragma once

#include <chrono>

#include "envoy/config/overload/v3/overload.pb.h"
#include "envoy/event/timer.h"
#include "envoy/server/instance.h"
#include "envoy/server/overload/overload_manager.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/stats.h"

#include "source/common/common/logger.h"

namespace Envoy {
namespace Server {

/**
 * Shuts the server down once the "envoy.overload_actions.shutdown" overload action has stayed
 * saturated for the configured duration, leaving it to a supervising process to restart Envoy.
 */
class OverloadShutdown : Logger::Loggable<Logger::Id::main> {
public:
  OverloadShutdown(Instance& server, OverloadManager& overload_manager, Stats::Scope& stats);

private:
  void onActionStateChanged(OverloadActionState state);
  void shutdownServer();
  std::chrono::milliseconds shutdownDelay() const;

  Instance& server_;
  std::chrono::milliseconds saturation_duration_{};
  std::chrono::milliseconds max_jitter_{};
  Stats::Counter* shutdown_counter_{};
  Event::TimerPtr saturation_timer_;
  bool shutting_down_{false};
};

} // namespace Server
} // namespace Envoy
