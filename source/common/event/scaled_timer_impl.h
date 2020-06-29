#pragma once

#include <chrono>

#include "envoy/common/scope_tracker.h"
#include "envoy/event/dispatcher.h"
#include "envoy/event/timer.h"

#include "absl/types/variant.h"

namespace Envoy {
namespace Event {

class ScaledTimerImpl : public ScaledTimer {
public:
  ScaledTimerImpl(Dispatcher& dispatcher, TimerCb cb, double scale_factor);

  // ScaledTimer
  void disableTimer() override;

  void enableTimer(const std::chrono::milliseconds& min, const std::chrono::milliseconds& max,
                   const ScopeTrackedObject* scope) override;
  void enableHRTimer(const std::chrono::microseconds& min, const std::chrono::microseconds& max,
                     const ScopeTrackedObject* object) override;

  bool enabled() override;

  /**
   * Adjusts the scale factor for the timeout, which is used to interpolate between the min and max
   * values set when the timer is enabled. A value of 0 causes the timer to use the minimum; a value
   * of 1 uses the maximum.
   *
   * This method is only provided on the impl because it should not be used by consumers of the
   * timer, only producers.
   * @param scale_factor The scale factor, which will be clipped to the range [0, 1].
   */
  void setScaleFactor(double scale_factor);

private:
  struct Enabled {
    Enabled();
    template <typename T> struct Interval {
      using value_type = T;
      Interval(T min, T max) : min(min), max(max) {}
      T min, max;
    };

    MonotonicTime last_enabled;
    absl::variant<Interval<std::chrono::milliseconds>, Interval<std::chrono::microseconds>>
        interval;
    const ScopeTrackedObject* scope;
  };

  double scale_factor_;
  TimerPtr timer_;
  TimeSource& time_source_;

  Enabled enabled_;
};

} // namespace Event
} // namespace Envoy