#pragma once

#include <functional>

#include "envoy/common/time.h"
#include "envoy/config/listener/v3/listener.pb.h"
#include "envoy/event/timer.h"
#include "envoy/server/drain_manager.h"
#include "envoy/server/instance.h"

#include "common/common/logger.h"

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
  bool drainClose() const override;

  // Server::DrainManager
  void startDrainSequence(std::function<void()> drain_complete_cb) override;
  bool draining() const override { return draining_; }
  void startParentShutdownSequence() override;

private:
  Instance& server_;
  const envoy::config::listener::v3::Listener::DrainType drain_type_;

  std::atomic<bool> draining_{false};
  Event::TimerPtr drain_tick_timer_;
  MonotonicTime drain_deadline_;

  Event::TimerPtr parent_shutdown_timer_;
};

} // namespace Server
} // namespace Envoy
