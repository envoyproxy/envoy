#pragma once

#include "envoy/event/timer.h"

#include "source/common/common/lock_guard.h"
#include "source/common/common/thread.h"

#include "test/test_common/test_random_generator.h"
#include "test/test_common/test_time_system.h"
#include "test/test_common/thread_factory_for_test.h"

#include "absl/container/flat_hash_map.h"

namespace Envoy {
namespace Event {

// Implements a simulated time system including a scheduler for timers. This is
// designed to be used as the exclusive time-system resident in a process at
// any particular time, and as such should not be instantiated directly by
// tests. Instead it should be instantiated via SimulatedTimeSystem, declared
// below.
class SimulatedTimeSystemHelper : public TestTimeSystem {
public:
  SimulatedTimeSystemHelper();
  ~SimulatedTimeSystemHelper() override;

  // TimeSystem
  SchedulerPtr createScheduler(Scheduler& base_scheduler, CallbackScheduler& cb_scheduler) override;

  // TestTimeSystem
  void advanceTimeWaitImpl(const Duration& duration) override;
  void advanceTimeAsyncImpl(const Duration& duration) override;

  // TimeSource
  SystemTime systemTime() override;
  MonotonicTime monotonicTime() override;

  /**
   * Sets the time forward monotonically. If the supplied argument moves
   * backward in time, the call is a no-op. If the supplied argument moves
   * forward, any applicable timers are fired, and system-time is also moved
   * forward by the same delta.
   *
   * @param monotonic_time The desired new current time.
   */
  void setMonotonicTime(const MonotonicTime& monotonic_time) {
    absl::MutexLock lock(&mutex_);
    setMonotonicTimeLockHeld(monotonic_time);
  }

  /**
   * Sets the system-time, whether forward or backward. If time moves forward,
   * applicable timers are fired and monotonic time is also increased by the
   * same delta.
   *
   * @param system_time The desired new system time.
   */
  void setSystemTime(const SystemTime& system_time);

  static bool hasInstance();

private:
  class SimulatedScheduler;
  class Alarm;

  void registerScheduler(SimulatedScheduler* scheduler) {
    absl::MutexLock lock(&mutex_);
    schedulers_.insert(scheduler);
  }

  void unregisterScheduler(SimulatedScheduler* scheduler) {
    absl::MutexLock lock(&mutex_);
    schedulers_.erase(scheduler);
  }

  /**
   * Sets the time forward monotonically. If the supplied argument moves
   * backward in time, the call is a no-op. If the supplied argument moves
   * forward, any applicable timers are fired, and system-time is also moved
   * forward by the same delta.
   *
   * @param monotonic_time The desired new current time.
   */
  void setMonotonicTimeLockHeld(const MonotonicTime& monotonic_time)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);

  // Keeps track of the number of simulated schedulers that have pending monotonic time updates.
  // Used by advanceTimeWait() to determine when the time updates have finished propagating.
  void incPending() {
    absl::MutexLock lock(&mutex_);
    ++pending_updates_;
  }
  void decPending() {
    absl::MutexLock lock(&mutex_);
    --pending_updates_;
  }
  void waitForNoPendingLockHeld() const ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);

  void maybeLogTimerWarning();

  RealTimeSource real_time_source_; // Used to initialize monotonic_time_ and system_time_;
  MonotonicTime monotonic_time_ ABSL_GUARDED_BY(mutex_);
  SystemTime system_time_ ABSL_GUARDED_BY(mutex_);
  TestRandomGenerator random_source_ ABSL_GUARDED_BY(mutex_);
  std::set<SimulatedScheduler*> schedulers_ ABSL_GUARDED_BY(mutex_);
  mutable absl::Mutex mutex_;
  uint32_t pending_updates_ ABSL_GUARDED_BY(mutex_){0};
  std::atomic<uint32_t> warning_logged_{};
};

// Represents a simulated time system, where time is advanced by calling
// sleep(), setSystemTime(), or setMonotonicTime(). systemTime() and
// monotonicTime() are maintained in the class, and alarms are fired in response
// to adjustments in time.
class SimulatedTimeSystem : public DelegatingTestTimeSystem<SimulatedTimeSystemHelper> {
public:
  void setMonotonicTime(const MonotonicTime& monotonic_time) {
    timeSystem().setMonotonicTime(monotonic_time);
  }
  void setSystemTime(const SystemTime& system_time) { timeSystem().setSystemTime(system_time); }

  template <class Duration> void setMonotonicTime(const Duration& duration) {
    setMonotonicTime(MonotonicTime(duration));
  }
  template <class Duration> void setSystemTime(const Duration& duration) {
    setSystemTime(SystemTime(duration));
  }
};

// Class encapsulating a SimulatedTimeSystem, intended for integration tests.
// Inherit from this mixin in a test fixture class to use a SimulatedTimeSystem
// during the test.
class TestUsingSimulatedTime {
public:
  SimulatedTimeSystem& simTime() { return sim_time_; }

private:
  SimulatedTimeSystem sim_time_;
};

} // namespace Event
} // namespace Envoy
