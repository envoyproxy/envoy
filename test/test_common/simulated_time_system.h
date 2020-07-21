#pragma once

#include "envoy/event/timer.h"

#include "common/common/lock_guard.h"
#include "common/common/thread.h"
#include "common/common/utility.h"

#include "test/test_common/only_one_thread.h"
#include "test/test_common/test_time_system.h"
#include "test/test_common/utility.h"

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
  void advanceTimeWait(const Duration& duration) override;
  void advanceTimeAsync(const Duration& duration) override;
  Thread::CondVar::WaitStatus waitFor(Thread::MutexBasicLockable& mutex, Thread::CondVar& condvar,
                                      const Duration& duration) noexcept
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex) override;

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
  friend class Alarm; // Needed to reference mutex for thread annotations.
  struct AlarmRegistration {
    AlarmRegistration(MonotonicTime time, uint64_t randomness, Alarm& alarm)
        : time_(time), randomness_(randomness), alarm_(alarm) {}

    MonotonicTime time_;
    // Random tie-breaker for alarms scheduled for the same monotonic time used to mimic
    // non-deterministic execution of real alarms scheduled for the same wall time.
    uint64_t randomness_;
    Alarm& alarm_;

    friend bool operator<(const AlarmRegistration& lhs, const AlarmRegistration& rhs) {
      if (lhs.time_ != rhs.time_) {
        return lhs.time_ < rhs.time_;
      }
      if (lhs.randomness_ != rhs.randomness_) {
        return lhs.randomness_ < rhs.randomness_;
      }
      // Out of paranoia, use pointer comparison on the alarms as a final tie-breaker but also
      // ASSERT that this branch isn't hit in debug modes since in practice the randomness_
      // associated with two registrations should never be equal.
      ASSERT(false, "Alarm registration randomness_ for two alarms should never be equal.");
      return &lhs.alarm_ < &rhs.alarm_;
    }
  };
  using AlarmSet = std::set<AlarmRegistration>;

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

  void alarmActivateLockHeld(Alarm& alarm) ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);

  // Adds/removes an alarm.
  void addAlarmLockHeld(Alarm&, const std::chrono::microseconds& duration)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);
  void removeAlarmLockHeld(Alarm&) ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);

  // Keeps track of how many alarms have been activated but not yet called,
  // which helps waitFor() determine when to give up and declare a timeout.
  void incPendingLockHeld() ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_) { ++pending_alarms_; }
  void decPending() {
    absl::MutexLock lock(&mutex_);
    decPendingLockHeld();
  }
  void decPendingLockHeld() ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_) { --pending_alarms_; }
  void waitForNoPendingLockHeld() const ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);

  RealTimeSource real_time_source_; // Used to initialize monotonic_time_ and system_time_;
  MonotonicTime monotonic_time_ ABSL_GUARDED_BY(mutex_);
  SystemTime system_time_ ABSL_GUARDED_BY(mutex_);
  TestRandomGenerator random_source_ ABSL_GUARDED_BY(mutex_);
  AlarmSet alarms_ ABSL_GUARDED_BY(mutex_);
  absl::flat_hash_map<Alarm*, AlarmSet::const_iterator>
      alarm_registrations_map_ ABSL_GUARDED_BY(mutex_);
  mutable absl::Mutex mutex_;
  uint32_t pending_alarms_ ABSL_GUARDED_BY(mutex_);
  Thread::OnlyOneThread only_one_thread_;
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
