#include "extensions/watchdog/abort_action/abort_action.h"

#include "common/common/assert.h"
#include "common/common/fmt.h"

#ifndef WIN32
#include <sys/types.h>
#include <csignal>
#endif

#include "common/common/logger.h"
#include "common/protobuf/utility.h"
#include "envoy/thread/thread.h"

namespace Envoy {
namespace Extensions {
namespace Watchdog {
namespace AbortAction {
namespace {
#ifdef __linux__
pid_t toPlatformTid(int64_t tid) { return static_cast<pid_t>(tid); }
#elif defined(__APPLE__)
uint64 toPlatformTid(int64_t tid) { return static_cast<uint64>(tid); }
#endif
} // namespace

AbortAction::AbortAction(
    envoy::extensions::watchdog::abort_action::v3alpha::AbortActionConfig& config,
    Server::Configuration::GuardDogActionFactoryContext& /*context*/)
    : config_(config){};

void AbortAction::run(
    envoy::config::bootstrap::v3::Watchdog::WatchdogAction::WatchdogEvent /*event*/,
    const std::vector<std::pair<Thread::ThreadId, MonotonicTime>>& thread_ltt_pairs,
    MonotonicTime /*now*/) {

  if (thread_ltt_pairs.empty()) {
    ENVOY_LOG_MISC(warn, "AbortAction called without any thread.");
    return;
  }

  int64_t raw_tid = thread_ltt_pairs.at(0).first.getId();

#ifdef WIN32
  // TODO(kbaichoo): add support for this with windows.
  ENVOY_LOG_MISC(error, "AbortAction is unimplemented for Windows.");
#else
  // Assume POSIX-compatible system and signal to the thread.
  ENVOY_LOG_MISC(error, "AbortAction sending abort signal to thread with tid {}.", raw_tid);

  if (kill(toPlatformTid(raw_tid), SIGABRT) == 0) {
    // Successfully sent signal, sleep for wait_duration.
    absl::SleepFor(absl::Milliseconds(PROTOBUF_GET_MS_OR_DEFAULT(config_, wait_duration, 0)));
  } else {
    // Failed to send the signal, abort?
    ENVOY_LOG_MISC(error, "Failed to send signal to tid {}", raw_tid);
  }

  // Abort from the action since the signaled thread hasn't yet crashed the process.
  PANIC(
      fmt::format("Failed to kill thread with id {}, aborting from AbortAction instead.", raw_tid));
#endif
}

} // namespace AbortAction
} // namespace Watchdog
} // namespace Extensions
} // namespace Envoy
