#pragma once

#include <chrono>
#include <functional>
#include <memory>

#include "envoy/common/pure.h"

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

} // namespace Event
} // namespace Envoy
