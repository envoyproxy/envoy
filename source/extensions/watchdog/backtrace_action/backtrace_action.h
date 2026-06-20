#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <csignal>

#include "envoy/event/timer.h"
#include "envoy/extensions/watchdog/backtrace_action/v3/backtrace_action.pb.h"
#include "envoy/server/guarddog_config.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/stats_macros.h"
#include "envoy/thread/thread.h"

#include "source/server/backtrace.h"

namespace Envoy {
namespace Extensions {
namespace Watchdog {
namespace BacktraceAction {

/**
 * All stats for the backtrace action. @see stats_macros.h
 */
#define ALL_BACKTRACE_ACTION_STATS(COUNTER)                                                        \
  COUNTER(backtraces_logged)                                                                       \
  COUNTER(backtraces_failed)

/**
 * Wrapper struct for the backtrace action stats. @see stats_macros.h
 */
struct BacktraceActionStats {
  ALL_BACKTRACE_ACTION_STATS(GENERATE_COUNTER_STRUCT)
};

/**
 * A GuardDogAction that logs backtraces of stuck threads.
 */
class BacktraceAction : public Server::Configuration::GuardDogAction {
public:
  BacktraceAction(envoy::extensions::watchdog::backtrace_action::v3::BacktraceActionConfig& config,
                  Server::Configuration::GuardDogActionFactoryContext& context);
  ~BacktraceAction() override;

  void run(envoy::config::bootstrap::v3::Watchdog::WatchdogAction::WatchdogEvent event,
           const std::vector<std::pair<Thread::ThreadId, MonotonicTime>>& thread_last_checkin_pairs,
           MonotonicTime now) override;

private:
  static constexpr int MaxSlots = 16;
  static constexpr int MaxStackDepth = 64;

  struct RawTrace {
    void* frames[MaxStackDepth];
    int depth{0};
  };

  struct SignalSlot {
    std::atomic<int64_t> tid{0}; // 0 means the slot is free.
    std::atomic<bool> ready{false};
    RawTrace trace{};
  };

  // Called in signal handler context; must be async-signal-safe.
  static void onNonFatalSignal(int sig, siginfo_t* info, void* context);

  static BacktraceActionStats generateStats(Stats::Scope& scope);

  // Minimum amount of time between backtraces for a given thread.
  std::chrono::milliseconds cooldown_duration_;

  BacktraceActionStats stats_;

  // Counts number of BacktraceAction instances sharing the onNonFatalSignal handler.
  static std::atomic<int> instance_count_;

  // Whether onNonFatalSignal is currently registered. Set when the first
  // instance successfully registers it.
  static std::atomic<bool> signal_handler_registered_;

  // Corresponding timer for each SignalSlot.
  std::array<Event::TimerPtr, MaxSlots> timers_;

  // Contains the backtrace state for up to MaxSlots threads at a time.
  // Must be static in case instance is destroyed while signal handler is using it.
  static std::array<SignalSlot, MaxSlots> signal_slots_;

  absl::flat_hash_map<Thread::ThreadId, MonotonicTime> tid_to_last_backtrace_;
};

using BacktraceActionPtr = std::unique_ptr<BacktraceAction>;

} // namespace BacktraceAction
} // namespace Watchdog
} // namespace Extensions
} // namespace Envoy
