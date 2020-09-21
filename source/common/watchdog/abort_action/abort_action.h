#pragma once

#include <chrono>

#include "envoy/server/guarddog_config.h"
#include "envoy/thread/thread.h"
#include "envoy/watchdog/abort_action/v3alpha/abort_action.pb.h"

namespace Envoy {
namespace Watchdog {
namespace AbortAction {

/**
 * A GuardDogAction that will terminate the process by killing the
 * stuck thread.
 */
class AbortAction : public Server::Configuration::GuardDogAction {
public:
  AbortAction(envoy::watchdog::abort_action::v3alpha::AbortActionConfig& config,
              Server::Configuration::GuardDogActionFactoryContext& context);

  void run(envoy::config::bootstrap::v3::Watchdog::WatchdogAction::WatchdogEvent event,
           const std::vector<std::pair<Thread::ThreadId, MonotonicTime>>& thread_last_checkin_pairs,
           MonotonicTime now) override;

private:
  const envoy::watchdog::abort_action::v3alpha::AbortActionConfig config_;
};

using AbortActionPtr = std::unique_ptr<AbortAction>;

} // namespace AbortAction
} // namespace Watchdog
} // namespace Envoy
