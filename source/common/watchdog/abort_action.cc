#include "source/common/watchdog/abort_action.h"

#include "envoy/thread/thread.h"

#include "source/common/common/assert.h"
#include "source/common/common/fmt.h"
#include "source/common/common/logger.h"
#include "source/common/protobuf/utility.h"
#include "source/common/thread/terminate_thread.h"

namespace Envoy {
namespace Watchdog {
namespace {
constexpr uint64_t DefaultWaitDurationMs = 5000;
} // end namespace

AbortAction::AbortAction(envoy::watchdog::v3::AbortActionConfig& config,
                         Server::Configuration::GuardDogActionFactoryContext& /*context*/)
    : wait_duration_(absl::Milliseconds(
          PROTOBUF_GET_MS_OR_DEFAULT(config, wait_duration, DefaultWaitDurationMs))) {}

void AbortAction::run(
    envoy::config::bootstrap::v3::Watchdog::WatchdogAction::WatchdogEvent /*event*/,
    const std::vector<std::pair<Thread::ThreadId, MonotonicTime>>& thread_last_checkin_pairs,
    MonotonicTime /*now*/) {

  if (thread_last_checkin_pairs.empty()) {
    ENVOY_LOG_MISC(warn, "Watchdog AbortAction called without any thread.");
    return;
  }

  // The following lines of code won't be considered covered by code coverage
  // tools since they would run in DEATH tests.
  const auto& thread_id = thread_last_checkin_pairs[0].first;
  const std::string tid_string = thread_id.debugString();
  ENVOY_LOG_MISC(error, "Watchdog AbortAction terminating thread with tid {}.", tid_string);

  if (Thread::terminateThread(thread_id)) {
    // Successfully signaled to thread to terminate, sleep for wait_duration.
    absl::SleepFor(wait_duration_);
  } else {
    ENVOY_LOG_MISC(error, "Failed to terminate tid {}", tid_string);
  }

  // Abort from the action since the signaled thread hasn't yet crashed the process.
  // Panicing in the action gives flexibility since it doesn't depend on
  // external code to kill the process if the signal fails.
  PANIC(fmt::format(
      "Failed to terminate thread with id {}, aborting from Watchdog AbortAction instead.",
      tid_string));
}

} // namespace Watchdog
} // namespace Envoy
