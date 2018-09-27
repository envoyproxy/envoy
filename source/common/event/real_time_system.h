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
  Thread::CondVar::WaitStatus
  waitFor(Thread::MutexBasicLockable& mutex, Thread::CondVar& condvar,
          const Duration& duration) noexcept EXCLUSIVE_LOCKS_REQUIRED(mutex) override;

  // TimeSource
  SystemTime systemTime() override { return time_source_.systemTime(); }
  MonotonicTime monotonicTime() override { return time_source_.monotonicTime(); }

private:
  RealTimeSource time_source_;
};

} // namespace Event
} // namespace Envoy
