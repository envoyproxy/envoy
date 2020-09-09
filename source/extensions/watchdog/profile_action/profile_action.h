#pragma once

#include <chrono>

#include "envoy/extensions/watchdog/profile_action/v3alpha/profile_action.pb.h"
#include "envoy/server/guarddog_config.h"
#include "envoy/thread/thread.h"

#include "absl/types/optional.h"

namespace Envoy {
namespace Extensions {
namespace Watchdog {
namespace ProfileAction {

/**
 * A GuardDogAction that will start CPU profiling.
 */
class ProfileAction : public Server::Configuration::GuardDogAction {
public:
  ProfileAction(envoy::extensions::watchdog::profile_action::v3alpha::ProfileActionConfig& config,
                Server::Configuration::GuardDogActionFactoryContext& context);

  void run(envoy::config::bootstrap::v3::Watchdog::WatchdogAction::WatchdogEvent event,
           const std::vector<std::pair<Thread::ThreadId, MonotonicTime>>& thread_ltt_pairs,
           MonotonicTime now) override;

private:
  // Helper to determine if we should run the profiler.
  absl::optional<Thread::ThreadId> getTidTriggeringProfile(
      const std::vector<std::pair<Thread::ThreadId, MonotonicTime>>& thread_ltt_pairs);

  const std::string path_;
  const std::chrono::milliseconds duration_;
  const uint64_t max_profiles_per_tid_;
  bool running_profile_;
  std::string profile_filename_;
  uint64_t profiles_started_;
  absl::flat_hash_map<Thread::ThreadId, uint64_t> tid_to_profile_count_;
  Server::Configuration::GuardDogActionFactoryContext& context_;
  Event::TimerPtr timer_cb_;
};

using ProfileActionPtr = std::unique_ptr<ProfileAction>;

} // namespace ProfileAction
} // namespace Watchdog
} // namespace Extensions
} // namespace Envoy
