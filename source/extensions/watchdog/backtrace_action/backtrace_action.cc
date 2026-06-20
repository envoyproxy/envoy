#include "source/extensions/watchdog/backtrace_action/backtrace_action.h"

#include <unistd.h>

#include "envoy/thread/thread.h"

#include "source/common/common/posix/thread_impl.h"
#include "source/common/protobuf/utility.h"
#include "source/common/signal/non_fatal_signal_handler.h"
#include "source/common/thread/signal_thread.h"

#include "absl/debugging/stacktrace.h"

namespace Envoy {
namespace Extensions {
namespace Watchdog {
namespace BacktraceAction {

std::array<BacktraceAction::SignalSlot, BacktraceAction::MaxSlots> BacktraceAction::signal_slots_;
std::atomic<int> BacktraceAction::instance_count_ = 0;
std::atomic<bool> BacktraceAction::signal_handler_registered_ = false;

BacktraceActionStats BacktraceAction::generateStats(Stats::Scope& scope) {
  return {ALL_BACKTRACE_ACTION_STATS(POOL_COUNTER_PREFIX(scope, "watchdog.backtrace_action."))};
}

BacktraceAction::BacktraceAction(
    envoy::extensions::watchdog::backtrace_action::v3::BacktraceActionConfig& config,
    Server::Configuration::GuardDogActionFactoryContext& context)
    : cooldown_duration_(
          std::chrono::milliseconds(PROTOBUF_GET_MS_OR_DEFAULT(config, cooldown_duration, 10000))),
      stats_(generateStats(context.stats_)) {
  if (instance_count_.fetch_add(1, std::memory_order_acq_rel) == 0) {
    signal_handler_registered_.store(
        NonFatalSignalHandler::registerNonFatalSignalHandler(onNonFatalSignal),
        std::memory_order_release);
  }

  for (int i = 0; i < MaxSlots; ++i) {
    timers_[i] = context.dispatcher_.createTimer([this, i]() {
      auto& slot = signal_slots_[i];
      if (slot.ready.load(std::memory_order_acquire)) {
        ENVOY_LOG_MISC(critical, "Backtrace Action: backtrace for thread {}:",
                       slot.tid.load(std::memory_order_relaxed));
        BackwardsTrace tracer(slot.trace.frames, slot.trace.depth);
        tracer.logTrace();
        stats_.backtraces_logged_.inc();
      } else {
        stats_.backtraces_failed_.inc();
      }
      slot.tid.store(0, std::memory_order_release);
    });
  }
}

BacktraceAction::~BacktraceAction() {
  if (instance_count_.fetch_sub(1, std::memory_order_acq_rel) == 1 &&
      signal_handler_registered_.load(std::memory_order_acquire)) {
    NonFatalSignalHandler::removeNonFatalSignalHandler(onNonFatalSignal);
    signal_handler_registered_.store(false, std::memory_order_release);
  }
}

void BacktraceAction::onNonFatalSignal(int /*sig*/, siginfo_t* info, void* context) {
  // Only handle signals sent by our own process.
  if (info == nullptr || info->si_pid != getpid()) {
    return;
  }
  // Async-signal-safe: reads a thread-local cached on each watched thread when
  // it registered with the watchdog (see worker_impl.cc / server.cc), so this
  // is just a TLS load by the time we reach the signal handler.
  const int64_t mytid = Thread::getCurrentThreadId();
  for (auto& slot : signal_slots_) {
    if (slot.tid.load(std::memory_order_acquire) == mytid) {
      auto& t = slot.trace;
      if (context != nullptr) {
        t.depth = absl::GetStackTraceWithContext(t.frames, MaxStackDepth, 1, context, nullptr);
      } else {
        t.depth = absl::GetStackTrace(t.frames, MaxStackDepth, 1);
      }
      slot.ready.store(true, std::memory_order_release);
      return;
    }
  }
}

void BacktraceAction::run(
    envoy::config::bootstrap::v3::Watchdog::WatchdogAction::WatchdogEvent /*event*/,
    const std::vector<std::pair<Thread::ThreadId, MonotonicTime>>& thread_last_checkin_pairs,
    MonotonicTime now) {
  if (!signal_handler_registered_.load(std::memory_order_acquire)) {
    ENVOY_LOG_MISC(warn, "Backtrace Action: signal handler not registered.");
    return;
  }
  if (thread_last_checkin_pairs.empty()) {
    ENVOY_LOG_MISC(warn, "Backtrace Action: no tids were provided.");
    return;
  }

  for (const auto& [tid, ltt] : thread_last_checkin_pairs) {
    if (auto it = tid_to_last_backtrace_.find(tid); it != tid_to_last_backtrace_.end()) {
      if (std::chrono::duration_cast<std::chrono::milliseconds>(now - it->second) <
          cooldown_duration_) {
        continue;
      }
    }

    const int64_t raw_tid = tid.getId();

    // Skip if already in-flight for this TID.
    bool pending = false;
    for (const auto& slot : signal_slots_) {
      if (slot.tid.load(std::memory_order_acquire) == raw_tid) {
        pending = true;
        break;
      }
    }
    if (pending) {
      continue;
    }

    // Claim a free slot.
    for (int i = 0; i < MaxSlots; ++i) {
      int64_t expected = 0;
      if (signal_slots_[i].tid.compare_exchange_strong(expected, raw_tid, std::memory_order_release,
                                                       std::memory_order_relaxed)) {
        signal_slots_[i].ready.store(false, std::memory_order_relaxed);
        if (!Thread::signalThread(tid, SIGUSR2)) {
          ENVOY_LOG_MISC(warn, "Backtrace Action: failed to signal thread {}.", raw_tid);
          signal_slots_[i].tid.store(0, std::memory_order_relaxed);
          stats_.backtraces_failed_.inc();
          break;
        }
        timers_[i]->enableTimer(std::chrono::milliseconds(100));
        tid_to_last_backtrace_[tid] = now;
        break;
      }
    }
  }
}

} // namespace BacktraceAction
} // namespace Watchdog
} // namespace Extensions
} // namespace Envoy
