#pragma once

#include <chrono>
#include <functional>
#include <memory>

#include "envoy/common/pure.h"
#include "envoy/common/time.h"

#include "common/common/thread.h"
#include "common/event/libevent.h"

namespace Envoy {
namespace Event {

class TimerCB;

/**
 * Callback invoked when a timer event fires.
 */
typedef std::function<void()> TimerCb;

/**
 * An abstract timer event. Free the timer to unregister any pending timeouts.
 */
class Timer {
public:
  virtual ~Timer() {}

  /**
   * Disable a pending timeout without destroying the underlying timer.
   */
  virtual void disableTimer() PURE;

  /**
   * Enable a pending timeout. If a timeout is already pending, it will be reset to the new timeout.
   */
  virtual void enableTimer(const std::chrono::milliseconds& d) PURE;
};

typedef std::unique_ptr<Timer> TimerPtr;

class Scheduler {
public:
  virtual ~Scheduler() {}

  /**
   * Creates a timer.
   */
  virtual TimerPtr createTimer(const TimerCb& cb) PURE;
};

typedef std::unique_ptr<Scheduler> SchedulerPtr;

/**
 * Interface providing a mechanism to measure time and set timers that run callbacks
 * when the timer fires.
 */
class TimeSystem : public TimeSource {
public:
  virtual ~TimeSystem() {}

  using Duration = MonotonicTime::duration;

  /**
   * Creates a timer factory. This indirection enables thread-local timer-queue management,
   * so servers can have a separate timer-factory in each thread.
   */
  virtual SchedulerPtr createScheduler(Libevent::BasePtr&) PURE;

  /**
   * Advances time forward by the specified duration, running any timers
   * along the way that have been scheduled to fire.
   *
   * @param duration The amount of time to sleep.
   */
  virtual void sleep(const Duration& duration) PURE;
  template <class D> void sleep(const D& duration) {
    sleep(std::chrono::duration_cast<Duration>(duration));
  }

  /**
   * Waits for the specified duration to expire, or for a condvar to
   * be notified, whichever comes first.
   *
   * @param mutex A mutex which must be held before calling this function.
   * @param condvar The condition to wait on.
   * @param duration The maximum amount of time to wait.
   * @return Thread::CondVar::WaitStatus whether the condition timed out or not.
   */
  virtual Thread::CondVar::WaitStatus
  waitFor(Thread::MutexBasicLockable& mutex, Thread::CondVar& condvar,
          const Duration& duration) noexcept EXCLUSIVE_LOCKS_REQUIRED(mutex) PURE;

  template <class D>
  Thread::CondVar::WaitStatus waitFor(Thread::MutexBasicLockable& mutex, Thread::CondVar& condvar,
                                      const D& duration) noexcept EXCLUSIVE_LOCKS_REQUIRED(mutex) {
    return waitFor(mutex, condvar, std::chrono::duration_cast<Duration>(duration));
  }
};

} // namespace Event
} // namespace Envoy
