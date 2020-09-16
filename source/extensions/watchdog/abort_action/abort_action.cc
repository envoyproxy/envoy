#include "extensions/watchdog/abort_action/abort_action.h"

#include <sys/types.h>

#include <csignal>

#include "envoy/thread/thread.h"

#include "common/common/assert.h"
#include "common/common/fmt.h"
#include "common/common/logger.h"
#include "common/protobuf/utility.h"

namespace Envoy {
namespace Extensions {
namespace Watchdog {
namespace AbortAction {
namespace {
#ifdef __linux__
pid_t toPlatformTid(int64_t tid) { return static_cast<pid_t>(tid); }
#elif defined(__APPLE__)
uint64_t toPlatformTid(int64_t tid) { return static_cast<uint64_t>(tid); }
#endif
} // namespace

AbortAction::AbortAction(
    envoy::extensions::watchdog::abort_action::v3alpha::AbortActionConfig& config,
    Server::Configuration::GuardDogActionFactoryContext& /*context*/)
    : config_(config){};

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
  int64_t raw_tid = thread_last_checkin_pairs[0].first.getId();

  // Assume POSIX-compatible system and signal to the thread.
  ENVOY_LOG_MISC(error, "Watchdog AbortAction sending abort signal to thread with tid {}.",
                 raw_tid);

  if (kill(toPlatformTid(raw_tid), SIGABRT) == 0) {
    // Successfully sent signal, sleep for wait_duration.
    absl::SleepFor(absl::Milliseconds(PROTOBUF_GET_MS_OR_DEFAULT(config_, wait_duration, 0)));
  } else {
    // Failed to send the signal, abort?
    ENVOY_LOG_MISC(error, "Failed to send signal to tid {}", raw_tid);
  }

  // Abort from the action since the signaled thread hasn't yet crashed the process.
  // panicing in the action gives flexibility since it doesn't depend on
  // external code to kill the process if the signal fails.
  PANIC(fmt::format("Failed to kill thread with id {}, aborting from Watchdog AbortAction instead.",
                    raw_tid));
}

} // namespace AbortAction
} // namespace Watchdog
} // namespace Extensions
} // namespace Envoy
