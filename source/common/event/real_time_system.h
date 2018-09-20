#pragma once

#include "envoy/event/timer.h"

#include "common/common/utility.h"

namespace Envoy {
namespace Event {

/**
 * Real-world time implementation of TimeSystem.
 */
class RealTimeSystem : public TimeSystem {
public:
  // TimeSystem
  SchedulerPtr createScheduler(Libevent::BasePtr&) override;
  void sleep(const Duration& duration) override;
  Thread::CondVar::WaitStatus waitFor(Thread::MutexBasicLockable& lock, Thread::CondVar& condvar,
                                      const Duration& duration) override;

  // TimeSource
  SystemTime systemTime() override { return time_source_.systemTime(); }
  MonotonicTime monotonicTime() override { return time_source_.monotonicTime(); }

private:
  RealTimeSource time_source_;
};

} // namespace Event
} // namespace Envoy
