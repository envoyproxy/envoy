#pragma once

#include "envoy/event/timer.h"

namespace Envoy {
namespace Event {

// Adds sleep() and waitFor() interfaces to Event::TimeSystem.
class TestTimeSystem : public Event::TimeSystem {
public:
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
