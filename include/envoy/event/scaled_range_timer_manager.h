#pragma once

#include "envoy/common/pure.h"
#include "envoy/event/timer.h"

#include "absl/types/variant.h"

namespace Envoy {
namespace Event {

/**
 * Describes a minimum timer value that is equal to a scale factor applied to the maximum.
 */
struct ScaledMinimum {
  explicit ScaledMinimum(double scale_factor) : scale_factor_(scale_factor) {}
  const double scale_factor_;
};

/**
 * Describes a minimum timer value that is an absolute duration.
 */
struct AbsoluteMinimum {
  explicit AbsoluteMinimum(std::chrono::milliseconds value) : value_(value) {}
  const std::chrono::milliseconds value_;
};

/**
 * Class that describes how to compute a minimum timeout given a maximum timeout value. It wraps
 * ScaledMinimum and AbsoluteMinimum and provides a single computeMinimum() method.
 */
class ScaledTimerMinimum : private absl::variant<ScaledMinimum, AbsoluteMinimum> {
public:
  // Use base class constructor.
  using absl::variant<ScaledMinimum, AbsoluteMinimum>::variant;

  // Computes the minimum value for a given maximum timeout. If this object was constructed with a
  // - ScaledMinimum value:
  //     the return value is the scale factor applied to the provided maximum.
  // - AbsoluteMinimum:
  //     the return value is that minimum, and the provided maximum is ignored.
  std::chrono::milliseconds computeMinimum(std::chrono::milliseconds maximum) const {
    struct Visitor {
      explicit Visitor(std::chrono::milliseconds value) : value_(value) {}
      std::chrono::milliseconds operator()(ScaledMinimum scale_factor) {
        return std::chrono::duration_cast<std::chrono::milliseconds>(scale_factor.scale_factor_ *
                                                                     value_);
      }
      std::chrono::milliseconds operator()(AbsoluteMinimum absolute_value) {
        return absolute_value.value_;
      }
      const std::chrono::milliseconds value_;
    };
    return absl::visit<Visitor, const absl::variant<ScaledMinimum, AbsoluteMinimum>&>(
        Visitor(maximum), *this);
  }
};

/**
 * Class for creating Timer objects that can be adjusted towards either the minimum or maximum
 * of their range by the owner of the manager object. Users of this class can call createTimer() to
 * receive a new Timer object that they can then enable or disable at will (but only on the same
 * dispatcher), and setScaleFactor() to change the scaling factor. The current scale factor is
 * applied to all timers, including those that are created later.
 */
class ScaledRangeTimerManager {
public:
  virtual ~ScaledRangeTimerManager() = default;

  /**
   * Creates a new timer backed by the manager. Calling enableTimer on the returned object sets the
   * maximum duration, while the first argument here controls the minimum. Passing a value of
   * ScaleFactor(x) sets the min to (x * max) when the timer is enabled, while AbsoluteValue(y) sets
   * the min to the duration y.
   */
  virtual TimerPtr createTimer(ScaledTimerMinimum minimum, TimerCb callback) PURE;

  /**
   * Sets the scale factor for all timers created through this manager. The value should be between
   * 0 and 1, inclusive. The scale factor affects the amount of time timers spend in their target
   * range. The timers returned by createTimer will fire after (min + (max - min) * scale_factor).
   * This means that a scale factor of 0 causes timers to fire immediately at the min duration, a
   * factor of 0.5 causes firing halfway between min and max, and a factor of 1 causes firing at
   * max.
   */
  virtual void setScaleFactor(double scale_factor) PURE;
};

using ScaledRangeTimerManagerPtr = std::unique_ptr<ScaledRangeTimerManager>;

} // namespace Event
} // namespace Envoy
