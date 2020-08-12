#include "extensions/watchdog/profile_action/profile_action.h"

#include <chrono>

#include "envoy/thread/thread.h"

#include "common/profiler/profiler.h"
#include "common/protobuf/utility.h"

#include "absl/strings/str_format.h"

namespace Envoy {
namespace Extensions {
namespace Watchdog {
namespace ProfileAction {
namespace {
static constexpr uint64_t DefaultMaxProfilePerTid = 10;

std::string generateProfileFilePath(const std::string& directory, const SystemTime& now) {
  auto timestamp = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
  if (absl::EndsWith(directory, "/")) {
    return absl::StrFormat("%s%s.%d", directory, "ProfileAction", timestamp);
  }
  return absl::StrFormat("%s/%s.%d", directory, "ProfileAction", timestamp);
}
} // namespace

ProfileAction::ProfileAction(
    envoy::extensions::watchdog::profile_action::v3alpha::ProfileActionConfig& config,
    Server::Configuration::GuardDogActionFactoryContext& context)
    : path_(config.profile_path()), running_profile_(false),
      max_profiles_per_tid_(config.max_profiles_per_thread() ? config.max_profiles_per_thread()
                                                             : DefaultMaxProfilePerTid),
      profiles_started_(0), duration_(std::chrono::milliseconds(
                                PROTOBUF_GET_MS_OR_DEFAULT(config, profile_duration, 5000))),
      context_(context) {}

void ProfileAction::run(
    envoy::config::bootstrap::v3::Watchdog::WatchdogAction::WatchdogEvent /*event*/,
    std::vector<std::pair<Thread::ThreadId, MonotonicTime>> thread_ltt_pairs,
    MonotonicTime /*now*/) {
  if (running_profile_) {
    return;
  }

  // Check if there's a tid that justifies profiling
  auto trigger_tid = getTidTriggeringProfile(thread_ltt_pairs);
  if (!trigger_tid.has_value()) {
    ENVOY_LOG_MISC(warn, "None of the provide tids justify profiling");
    return;
  }

  auto& fs = context_.api_.fileSystem();
  if (!fs.directoryExists(path_)) {
    ENVOY_LOG_MISC(error, "Directory path {} doesn't exists.", path_);
    return;
  }

  // Generate file path for output and try to profile
  const auto& profile_filename =
      generateProfileFilePath(path_, context_.api_.timeSource().systemTime());

  if (!Profiler::Cpu::profilerEnabled()) {
    if (Profiler::Cpu::startProfiler(profile_filename)) {
      // Update state
      running_profile_ = true;
      ++profiles_started_;
      tid_to_profile_count_[*trigger_tid] += 1;

      // Schedule callback to stop
      timer_cb_ = context_.dispatcher_.createTimer([this, profile_filename] {
        if (Profiler::Cpu::profilerEnabled()) {
          Profiler::Cpu::stopProfiler();
          running_profile_ = false;
          timer_cb_->disableTimer();
        } else {
          ENVOY_LOG_MISC(error,
                         "Profile Action's stop() was scheduled, but profiler isn't running!");
        }

        if (!context_.api_.fileSystem().fileExists(profile_filename)) {
          ENVOY_LOG_MISC(error, "Profile file {} wasn't created!", profile_filename);
        }
      });

      timer_cb_->enableTimer(duration_);
    } else {
      ENVOY_LOG_MISC(warn, "Profile Action failed to start the profiler.");
    }
  } else {
    ENVOY_LOG_MISC(warn, "Profile Action unable to start the profiler as it is in use elsewhere.");
  }
}

// Helper to determine if we have a valid tid to start profiling.
absl::optional<Thread::ThreadId> ProfileAction::getTidTriggeringProfile(
    const std::vector<std::pair<Thread::ThreadId, MonotonicTime>>& thread_ltt_pairs) {
  absl::optional<Thread::ThreadId> tid_to_profile;

  // Find a TID not over the max_profiles threshold
  for (const auto& tid_time_pair : thread_ltt_pairs) {
    const auto tid = tid_time_pair.first;

    if (tid_to_profile_count_[tid] < max_profiles_per_tid_) {
      tid_to_profile = tid;
      break;
    }
  }

  return tid_to_profile;
}
} // namespace ProfileAction
} // namespace Watchdog
} // namespace Extensions
} // namespace Envoy
