#pragma once

#include <chrono>

#include "envoy/extensions/watchdog/abort_action/v3alpha/abort_action.pb.h"
#include "envoy/server/guarddog_config.h"
#include "envoy/thread/thread.h"

namespace Envoy {
namespace Extensions {
namespace Watchdog {
namespace AbortAction {

/**
 * A GuardDogAction that will start signal to a particular thread to abort.
 * This is currently only implemented for systems that support kill to send
 * signals.
 */
class AbortAction : public Server::Configuration::GuardDogAction {
public:
  AbortAction(envoy::extensions::watchdog::abort_action::v3alpha::AbortActionConfig& config,
              Server::Configuration::GuardDogActionFactoryContext& context);

  void run(envoy::config::bootstrap::v3::Watchdog::WatchdogAction::WatchdogEvent event,
           const std::vector<std::pair<Thread::ThreadId, MonotonicTime>>& thread_ltt_pairs,
           MonotonicTime now) override;

private:
  const envoy::extensions::watchdog::abort_action::v3alpha::AbortActionConfig config_;
};

using AbortActionPtr = std::unique_ptr<AbortAction>;

} // namespace AbortAction
} // namespace Watchdog
} // namespace Extensions
} // namespace Envoy
