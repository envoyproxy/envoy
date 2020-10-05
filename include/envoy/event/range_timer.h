#pragma once

#include <chrono>
#include <functional>
#include <memory>

#include "envoy/common/pure.h"
#include "envoy/common/time.h"
#include "envoy/event/timer.h"

namespace Envoy {
namespace Event {

/**
 * An abstract event timer that can be scheduled for a timeout within a range. The actual timeout
 * used is left up to individual implementations.
 */
class RangeTimer {
public:
  virtual ~RangeTimer() = default;

  /**
   * Disable a pending timeout without destroying the underlying timer.
   */
  virtual void disableTimer() PURE;

  /**
   * Enable a pending timeout within the given range. If a timeout is already pending, it will be
   * reset to the new timeout.
   *
   * @param min_ms supplies the minimum duration of the alarm in milliseconds.
   * @param max_ms supplies the maximum duration of the alarm in milliseconds.
   * @param object supplies an optional scope for the duration of the alarm.
   */
  virtual void enableTimer(std::chrono::milliseconds min_ms, std::chrono::milliseconds max_ms,
                           const ScopeTrackedObject* object = nullptr) PURE;

  /**
   * Return whether the timer is currently armed.
   */
  virtual bool enabled() PURE;
};

using RangeTimerPtr = std::unique_ptr<RangeTimer>;

} // namespace Event
} // namespace Envoy