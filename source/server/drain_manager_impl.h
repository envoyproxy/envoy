#pragma once

#include <chrono>
#include <functional>
#include <vector>

#include "envoy/common/time.h"
#include "envoy/config/listener/v3/listener.pb.h"
#include "envoy/event/dispatcher.h"
#include "envoy/event/timer.h"
#include "envoy/server/drain_manager.h"
#include "envoy/server/instance.h"

#include "source/common/common/callback_impl.h"
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
  DrainManagerImpl(Instance& server, envoy::config::listener::v3::Listener::DrainType drain_type,
                   Event::Dispatcher& dispatcher);

  // Network::DrainDecision
  bool drainClose(Network::DrainDirection scope) const override;
  Common::CallbackHandlePtr addOnDrainCloseCb(Network::DrainDirection direction,
                                              DrainCloseCb cb) const override;

  // Server::DrainManager
  void startDrainSequence(Network::DrainDirection direction,
                          std::function<void()> drain_complete_cb) override;
  bool draining(Network::DrainDirection direction) const override {
    auto drain_pair = draining_.load();
    return drain_pair.first && direction <= drain_pair.second;
  }
  void startParentShutdownSequence() override;
  DrainManagerPtr
  createChildManager(Event::Dispatcher& dispatcher,
                     envoy::config::listener::v3::Listener::DrainType drain_type) override;
  DrainManagerPtr createChildManager(Event::Dispatcher& dispatcher) override;

private:
  void addDrainCompleteCallback(Network::DrainDirection direction, std::function<void()> cb);

  Instance& server_;
  Event::Dispatcher& dispatcher_;
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
  mutable Common::CallbackManager<absl::Status, std::chrono::milliseconds> cbs_{};
  std::vector<std::function<void()>> drain_complete_cbs_;

  // Callbacks called by startDrainSequence to cascade/proxy to children
  std::shared_ptr<Common::ThreadSafeCallbackManager> children_;

  // Callback handle parent will invoke to initiate drain-sequence. Created and set
  // by the parent drain-manager.
  Common::CallbackHandlePtr parent_callback_handle_;

  Event::TimerPtr parent_shutdown_timer_;
};

} // namespace Server
} // namespace Envoy
