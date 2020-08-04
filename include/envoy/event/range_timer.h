#pragma once

#include <chrono>
#include <memory>

#include "envoy/common/pure.h"
#include "envoy/common/time.h"

namespace Envoy {

class ScopeTrackedObject;

namespace Event {

using RangeTimerCb = std::function<void()>;

/**
 * An abstract event timer that can be scheduled for a timeout within a range.
 */
class RangeTimer {
public:
  virtual ~RangeTimer() = default;

  /**
   * Disable a pending timeout without destroying the underlying timer.
   */
  virtual void disableTimer() PURE;

  /**
   * Enable a pending timeout within the given range. Unless disabled, the timer will fire exactly
   * once, after min_ms has elapsed and before max_ms has elapsed. If a timeout is already pending,
   * it will be reset to the new timeout.
   *
   * @param min_ms supplies the minimum duration of the alarm in milliseconds.
   * @param max_ms supplies the maximum duration of the alarm in milliseconds.
   * @param object supplies an optional scope for the duration of the alarm.
   */
  virtual void enableTimer(const std::chrono::milliseconds& min_ms,
                           const std::chrono::milliseconds& max_ms,
                           const ScopeTrackedObject* object = nullptr) PURE;

  /**
   * Return whether the timer is currently armed.
   */
  virtual bool enabled() PURE;
};

using RangeTimerPtr = std::unique_ptr<RangeTimer>;

} // namespace Event
} // namespace Envoy
