#pragma once

#include "envoy/event/timer.h"

#include "common/common/lock_guard.h"
#include "common/common/thread.h"
#include "common/common/utility.h"

namespace Envoy {
namespace Event {

// Represents a simulated time system, where time is advanced by calling
// sleep(). systemTime() and monotonicTime() are computed from the sleeps,
// and alarms are fired in response to sleep as well.
class SimulatedTimeSystem : public TimeSystem {
public:
  class Alarm;

  SimulatedTimeSystem();

  // TimeSystem
  SchedulerPtr createScheduler(Libevent::BasePtr&) override;

  // TimeSource
  SystemTime systemTime() override;
  MonotonicTime monotonicTime() override;

  /**
   * Advances time forward by the specified duration, running any timers
   * along the way that have been scheduled to fire.
   *
   * @param duration The amount of time to sleep.
   */
  template <class Duration> void sleep(const Duration& duration) {
    mutex_.lock();
    MonotonicTime monotonic_time = monotonic_time_ + duration;
    setMonotonicTimeAndUnlock(monotonic_time);
  }

  /**
   * Sets the time forward monotonically. if the supplied argument moves
   * backward in time, the call is a no-op.
   *
   * @param monotonic_time The desired new current time.
   */
  void setMonotonicTime(const MonotonicTime& monotonic_time) {
    mutex_.lock();
    setMonotonicTimeAndUnlock(monotonic_time);
  }

  /**
   * Sets the system-time forward. if the supplied argument moves
   * backward in time, the call works, but of course we can't uncall
   * any fired alarms.
   *
   * @param system_time The desired new system time.
   */
  void setSystemTime(const SystemTime& system_time);

private:
  void setMonotonicTimeAndUnlock(const MonotonicTime& monotonic_time) UNLOCK_FUNCTION(mutex_);

  // The simulation keeps a unique ID for each alarm to act as a deterministic
  // tie-breaker for alarm-ordering.
  int64_t nextIndex();

  // Adds/removes an alarm.
  void addAlarm(Alarm*, const std::chrono::milliseconds& duration);
  void removeAlarm(Alarm*);

  friend class SimulatedScheduler;
  friend Alarm;
  struct CompareAlarms {
    bool operator()(const Alarm* a, const Alarm* b) const;
  };
  typedef std::set<Alarm*, CompareAlarms> AlarmSet;

  RealTimeSource real_time_source_;
  MonotonicTime monotonic_time_ GUARDED_BY(mutex_);
  SystemTime system_time_ GUARDED_BY(mutex_);
  ;
  AlarmSet alarms_ GUARDED_BY(mutex_);
  uint64_t index_ GUARDED_BY(mutex_);

  mutable Thread::MutexBasicLockable mutex_;
};

} // namespace Event
} // namespace Envoy
