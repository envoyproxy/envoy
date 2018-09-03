#pragma once

#include "envoy/event/timer.h"

#include "common/common/utility.h"
#include "common/common/thread.h"

namespace Envoy {
namespace Event {

class SimulatedTimeSystem : public TimeSystem {
public:
  // TimeSystem
  TimerFactoryPtr createScheduler(Libevent::BasePtr&) override;

  // TimeSource
  SystemTime systemTime() override { return system_time_; }
  MonotonicTime monotonicTime() override { return monotonic_time_; }

  // Advances time forward by the specified duration, running any timers
  // that are scheduled to wake up.
  void sleep(std::chrono::duration);

private:
  friend class SimulatedScheduler;

  void removeScheduler(SimulatedScheduler*);

  RealTimeSource real_time_source_ GUARDED_BY(mutex_);
  MonotonicTime monotonic_time_ GUARDED_BY(mutex_);
  SystemTime system_time_ GUARDED_BY(mutex_);;
  std:::unordered_set<SimulatedScheduler*> schedulers_ GUARDED_BY(mutex_);
  Thread::MutexBasicLockable mutex_;
};

} // namespace Event
} // namespace Envoy
