#pragma once

#include "envoy/common/pure.h"
#include "envoy/event/range_timer.h"
#include "envoy/event/timer.h"

namespace Envoy {
namespace Event {

/**
 * Class for creating RangeTimer objects that can be adjusted towards either the minimum or maximum
 * of their range by the owner of the manager object. Users of this class can call createTimer() to
 * receive a new RangeTimer object that they can then enable or disable at will (but only on the
 * same dispatcher), and setScaleFactor() to change the scaling factor. The current scale factor is
 * applied to all timers, including those that are created later.
 */
class ScaledRangeTimerManager {
public:
  virtual ~ScaledRangeTimerManager() = default;

  /**
   * Creates a new range timer backed by the manager. The returned timer will be subject to the
   * current and future scale factor values set on the manager. All returned timers must be deleted
   * before the manager.
   */
  virtual RangeTimerPtr createTimer(TimerCb callback) PURE;

  /**
   * Sets the scale factor for all timers created through this manager. The value should be between
   * 0 and 1, inclusive. The scale factor affects the amount of time timers spend in their target
   * range. The RangeTimers returned by createTimer will fire after (min + (max - min) *
   * scale_factor). This means that a scale factor of 0 causes timers to fire immediately at the min
   * duration, a factor of 0.5 causes firing halfway between min and max, and a factor of 1 causes
   * firing at max.
   */
  virtual void setScaleFactor(double scale_factor) PURE;
};

using ScaledRangeTimerManagerPtr = std::unique_ptr<ScaledRangeTimerManager>;

} // namespace Event
} // namespace Envoy
