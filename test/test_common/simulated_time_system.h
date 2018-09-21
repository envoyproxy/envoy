#pragma once

#include "envoy/event/timer.h"

#include "common/common/lock_guard.h"
#include "common/common/thread.h"
#include "common/common/utility.h"

namespace Envoy {
namespace Event {

// Represents a simulated time system, where time is advanced by calling
// sleep(), setSystemTime(), or setMonotonicTime(). systemTime() and
// monotonicTime() are maintained in the class, and alarms are fired in response
// to adjustments in time.
class SimulatedTimeSystem : public TimeSystem {
public:
  SimulatedTimeSystem();
  ~SimulatedTimeSystem();

  // TimeSystem
  SchedulerPtr createScheduler(Libevent::BasePtr&) override;

  // TimeSource
  SystemTime systemTime() override;
  MonotonicTime monotonicTime() override;
  void sleep(const Duration& duration) override;
  Thread::CondVar::WaitStatus
  waitFor(Thread::MutexBasicLockable& mutex, Thread::CondVar& condvar,
          const Duration& duration) noexcept EXCLUSIVE_LOCKS_REQUIRED(mutex) override;

  /**
   * Sets the time forward monotonically. If the supplied argument moves
   * backward in time, the call is a no-op. If the supplied argument moves
   * forward, any applicable timers are fired, and system-time is also moved
   * forward by the same delta.
   *
   * @param monotonic_time The desired new current time.
   */
  void setMonotonicTime(const MonotonicTime& monotonic_time) {
    mutex_.lock();
    setMonotonicTimeAndUnlock(monotonic_time);
  }
  template <class Duration> void setMonotonicTime(const Duration& duration) {
    setMonotonicTime(MonotonicTime(duration));
  }

  /**
   * Sets the system-time, whether forward or backward. If time moves forward,
   * applicable timers are fired and monotonic time is also increased by the
   * same delta.
   *
   * @param system_time The desired new system time.
   */
  void setSystemTime(const SystemTime& system_time);
  template <class Duration> void setSystemTime(const Duration& duration) {
    setSystemTime(SystemTime(duration));
  }

private:
  class SimulatedScheduler;
  class Alarm;
  struct CompareAlarms {
    bool operator()(const Alarm* a, const Alarm* b) const;
  };
  using AlarmSet = std::set<Alarm*, CompareAlarms>;

  /**
   * Sets the time forward monotonically. If the supplied argument moves
   * backward in time, the call is a no-op. If the supplied argument moves
   * forward, any applicable timers are fired, and system-time is also moved
   * forward by the same delta.
   *
   * @param monotonic_time The desired new current time.
   */
  void setMonotonicTimeAndUnlock(const MonotonicTime& monotonic_time) UNLOCK_FUNCTION(mutex_);

  // The simulation keeps a unique ID for each alarm to act as a deterministic
  // tie-breaker for alarm-ordering.
  int64_t nextIndex();

  // Adds/removes an alarm.
  void addAlarm(Alarm*, const std::chrono::milliseconds& duration);
  void removeAlarm(Alarm*);

  // Keeps track of how many alarms have been activated but not yet called,
  // which helps waitFor() determine when to give up and declare a timeout.
  void incPending() { ++pending_alarms_; }
  void decPending() { --pending_alarms_; }
  bool hasPending() const { return pending_alarms_ > 0; }

  RealTimeSource real_time_source_; // Used to initialize monotonic_time_ and system_time_;
  MonotonicTime monotonic_time_ GUARDED_BY(mutex_);
  SystemTime system_time_ GUARDED_BY(mutex_);
  AlarmSet alarms_ GUARDED_BY(mutex_);
  uint64_t index_ GUARDED_BY(mutex_);
  mutable Thread::MutexBasicLockable mutex_;
  std::atomic<uint32_t> pending_alarms_;
};

} // namespace Event
} // namespace Envoy
