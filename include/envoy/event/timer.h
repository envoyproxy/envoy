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
};

} // namespace Event
} // namespace Envoy
