#pragma once

#include <functional>
#include <vector>

#include "envoy/common/time.h"
#include "envoy/config/listener/v3/listener.pb.h"
#include "envoy/event/timer.h"
#include "envoy/server/drain_manager.h"
#include "envoy/server/instance.h"

#include "source/common/common/logger.h"

namespace Envoy {
namespace Server {

/**
 * Implementation of drain manager that does the following by default:
 * 1) Terminates the parent process after 15 minutes.
 * 2) Drains the parent process over a period of 10 minutes where drain close becomes more
 *    likely each second that passes.
 */
class DrainManagerImpl : Logger::Loggable<Logger::Id::main>, public DrainManager {
public:
  DrainManagerImpl(Instance& server, envoy::config::listener::v3::Listener::DrainType drain_type);

  // Network::DrainDecision
  bool drainClose(Network::DrainDirection scope) const override;

  // Server::DrainManager
  void startDrainSequence(Network::DrainDirection direction,
                          std::function<void()> drain_complete_cb) override;
  bool draining(Network::DrainDirection direction) const override {
    return draining_.load().first && direction <= draining_.load().second;
  }
  void startParentShutdownSequence() override;

private:
  Instance& server_;
  const envoy::config::listener::v3::Listener::DrainType drain_type_;
  using DrainPair = struct {
    bool first;
    Network::DrainDirection second;
  };
  std::atomic<DrainPair> draining_{DrainPair{false, Network::DrainDirection::None}};
  // A map of timers keyed by the direction that triggered the drain
  std::map<Network::DrainDirection, Event::TimerPtr> drain_tick_timers_;
  std::map<Network::DrainDirection, MonotonicTime> drain_deadlines_ = {
      {Network::DrainDirection::InboundOnly, MonotonicTime()},
      {Network::DrainDirection::All, MonotonicTime()}};

  Event::TimerPtr parent_shutdown_timer_;
};

} // namespace Server
} // namespace Envoy
