#pragma once

#include <chrono>
#include <functional>
#include <memory>

#include "envoy/common/pure.h"
#include "envoy/common/time.h"

namespace Envoy {
namespace Event {

/**
 * Callback invoked when a timer event fires.
 */
using TimerCb = std::function<void()>;

/**
 * An abstract timer event. Free the timer to unregister any pending timeouts.
 */
class Timer {
public:
  virtual ~Timer() = default;

  /**
   * Disable a pending timeout without destroying the underlying timer.
   */
  virtual void disableTimer() PURE;

  /**
   * Enable a pending timeout. If a timeout is already pending, it will be reset to the new timeout.
   */
  virtual void enableTimer(const std::chrono::milliseconds& d) PURE;

  /**
   * Return whether the timer is currently armed.
   */
  virtual bool enabled() PURE;
};

using TimerPtr = std::unique_ptr<Timer>;

class Scheduler {
public:
  virtual ~Scheduler() = default;

  /**
   * Creates a timer.
   */
  virtual TimerPtr createTimer(const TimerCb& cb) PURE;
};

using SchedulerPtr = std::unique_ptr<Scheduler>;

/**
 * Interface providing a mechanism to measure time and set timers that run callbacks
 * when the timer fires.
 */
class TimeSystem : public TimeSource {
public:
  ~TimeSystem() override = default;

  using Duration = MonotonicTime::duration;

  /**
   * Creates a timer factory. This indirection enables thread-local timer-queue management,
   * so servers can have a separate timer-factory in each thread.
   */
  virtual SchedulerPtr createScheduler(Scheduler& base_scheduler) PURE;
};

} // namespace Event
} // namespace Envoy
