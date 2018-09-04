#pragma once

#include "envoy/event/timer.h"

#include "common/common/lock_guard.h"
#include "common/common/thread.h"
#include "common/common/utility.h"

namespace Envoy {
namespace Event {

class SimulatedTimeSystem : public TimeSystem {
public:
  class Alarm;

  SimulatedTimeSystem();

  // TimeSystem
  SchedulerPtr createScheduler(Libevent::BasePtr&) override;

  // TimeSource
  SystemTime systemTime() override;
  MonotonicTime monotonicTime() override;

  // Advances time forward by the specified duration, running any timers
  // that are scheduled to wake up.
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

  int64_t nextIndex();
  void addAlarm(Alarm*, const std::chrono::milliseconds& duration);
  void removeAlarm(Alarm*);

private:
  friend class SimulatedScheduler;
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
