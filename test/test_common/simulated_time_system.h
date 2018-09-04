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
  template<class Duration>
  void sleep(Duration duration) {
    std::vector<Alarm*> ready;
    {
      Thread::LockGuard lock(mutex_);
      monotonic_time_ += duration;
      system_time_ += duration;
      ready = findReadyAlarmsLockHeld();
    }
    runAlarms(ready);
  }

private:
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

  std::vector<Alarm*> findReadyAlarmsLockHeld() EXCLUSIVE_LOCKS_REQUIRED(mutex_);
  void runAlarms(const std::vector<Alarm*> alarms);

  RealTimeSource real_time_source_;
  MonotonicTime monotonic_time_ GUARDED_BY(mutex_);
  SystemTime system_time_ GUARDED_BY(mutex_);;
  AlarmSet alarms_ GUARDED_BY(mutex_);
  uint64_t index_ GUARDED_BY(mutex_);

  mutable Thread::MutexBasicLockable mutex_;
};

} // namespace Event
} // namespace Envoy
