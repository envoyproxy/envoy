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
    timers_[i] = context.dispatcher_.createTimer([this, i]() { onSlotTimer(i); });
  }
}

void BacktraceAction::onSlotTimer(int slot_index) {
  auto& slot = signal_slots_[slot_index];
  switch (slot.state.load(std::memory_order_acquire)) {
  case SlotState::Ready: {
    // The handler finished writing the backtrace and it's safe to read.
    // Log it and free the slot for future use.
    ENVOY_LOG_MISC(critical, "Backtrace Action: backtrace for thread {}:",
                   slot.tid.load(std::memory_order_relaxed));
    BackwardsTrace tracer(slot.trace.frames, slot.trace.depth);
    tracer.logTrace();
    stats_.backtraces_logged_.inc();
    slot.state.store(SlotState::Free, std::memory_order_release);
    break;
  }
  case SlotState::Signaled: {
    // The target thread never entered the signal handler. Assume that the
    // thread is unable to process the signal and attempt to free the slot.
    SlotState expected = SlotState::Signaled;
    if (slot.state.compare_exchange_strong(expected, SlotState::Free, std::memory_order_acq_rel,
                                           std::memory_order_relaxed)) {
      stats_.backtraces_failed_.inc();
      break;
    }
    // The target thread's signal handler just started writing the backtrace.
    // Fall through and wait for it to finish.
    FALLTHRU;
  }
  case SlotState::Writing:
    // The handler is writing the backtrace into this slot at this moment.
    // Re-arm the timer to let it finish.
    timers_[slot_index]->enableTimer(std::chrono::milliseconds(100));
    break;
  case SlotState::Claimed:
  case SlotState::Free:
    // Nothing to do: Free means the slot was already released, and Claimed
    // cannot be observed here (the timer is only armed after Signaled).
    break;
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

  // Claim the slot identified by the TID of this thread.
  const int64_t mytid = Thread::getCurrentThreadId();
  for (auto& slot : signal_slots_) {
    if (slot.state.load(std::memory_order_acquire) != SlotState::Signaled ||
        slot.tid.load(std::memory_order_relaxed) != mytid) {
      continue;
    }
    // Take ownership of the slot's backtrace buffer for the duration of the
    // write. If the CAS fails, the timer freed the slot; nothing to do.
    SlotState expected = SlotState::Signaled;
    if (!slot.state.compare_exchange_strong(expected, SlotState::Writing, std::memory_order_acq_rel,
                                            std::memory_order_relaxed)) {
      continue;
    }
    auto& t = slot.trace;
    if (context != nullptr) {
      t.depth = absl::GetStackTraceWithContext(t.frames, MaxStackDepth, 1, context, nullptr);
    } else {
      t.depth = absl::GetStackTrace(t.frames, MaxStackDepth, 1);
    }
    // Publish the completed backtrace to the timer.
    slot.state.store(SlotState::Ready, std::memory_order_release);
    return;
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
    // Skip any threads that are still cooling off.
    if (auto it = tid_to_last_backtrace_.find(tid); it != tid_to_last_backtrace_.end()) {
      if (std::chrono::duration_cast<std::chrono::milliseconds>(now - it->second) <
          cooldown_duration_) {
        continue;
      }
    }

    // Claim a free slot and signal the target thread to collect a backtrace.
    const int64_t raw_tid = tid.getId();
    for (int i = 0; i < MaxSlots; ++i) {
      auto& slot = signal_slots_[i];
      SlotState expected = SlotState::Free;
      if (!slot.state.compare_exchange_strong(
              expected, SlotState::Claimed, std::memory_order_acq_rel, std::memory_order_relaxed)) {
        continue;
      }
      slot.tid.store(raw_tid, std::memory_order_relaxed);
      slot.state.store(SlotState::Signaled, std::memory_order_release);
      if (!Thread::signalThread(tid, SIGUSR2)) {
        ENVOY_LOG_MISC(warn, "Backtrace Action: failed to signal thread {}.", raw_tid);
        slot.state.store(SlotState::Free, std::memory_order_release);
        stats_.backtraces_failed_.inc();
        break;
      }
      timers_[i]->enableTimer(std::chrono::milliseconds(100));
      tid_to_last_backtrace_[tid] = now;
      break;
    }
  }
}

} // namespace BacktraceAction
} // namespace Watchdog
} // namespace Extensions
} // namespace Envoy
