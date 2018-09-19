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

  // TimeSystem
  SchedulerPtr createScheduler(Libevent::BasePtr&) override;

  // TimeSource
  SystemTime systemTime() override;
  MonotonicTime monotonicTime() override;

  /**
   * Advances time forward by the specified duration, running any timers
   * along the way that have been scheduled to fire.
   *
   * @param duration The amount of time to sleep, expressed in any type that
   * can be duration_casted to MonotonicTime::duration.
   */
  template <class Duration> void sleep(const Duration& duration) {
    mutex_.lock();
    MonotonicTime monotonic_time =
        monotonic_time_ + std::chrono::duration_cast<MonotonicTime::duration>(duration);
    setMonotonicTimeAndUnlock(monotonic_time);
  }

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

  RealTimeSource real_time_source_; // Used to initialize monotonic_time_ and system_time_;
  MonotonicTime monotonic_time_ GUARDED_BY(mutex_);
  SystemTime system_time_ GUARDED_BY(mutex_);
  AlarmSet alarms_ GUARDED_BY(mutex_);
  uint64_t index_ GUARDED_BY(mutex_);
  mutable Thread::MutexBasicLockable mutex_;
};

} // namespace Event
} // namespace Envoy
