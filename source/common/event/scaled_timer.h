#include "envoy/common/time.h"
#include "envoy/event/dispatcher.h"
#include "envoy/event/timer.h"
#include "envoy/common/scope_tracker.h"
#include "common/common/scope_tracker.h"
#include "envoy/common/time.h"
#include <chrono>

namespace Envoy {
namespace Event {
/**
 * Utility class for maintaining a list of timers whose durations can be scaled by an arbitrary
 * value. The timer objects returned by createTimer() must not outlive the manager.
 */
class ScaledTimerManager {
public:
  ScaledTimerManager(Dispatcher& dispatcher);
  /**
   * Creates a timer that will fire the given callback when triggered. The returned timer object
   * must not outlive the dispatcher.
   */
  TimerPtr createTimer(TimerCb callback);
  /**
   * Sets the scale factor for the duration. A value of 0 will cause all timers to be triggered
   * immediately, and a value of 1 will cause them to schedule at the usual time.
   * @param scale the scale factor to apply; must be between 0 and 1.
   */
  void setDurationScaleFactor(double scale);

private:
  // Forward-declare impl class; this is defined in the .cc file.
  class ScaledTimerImpl;

  class ScaledTimerList {
  public:
    ScaledTimerList(double scale_factor);
    void addActive(ScaledTimerImpl* tracker);
    void removeActive(ScaledTimerImpl* tracker);
    void setScaleFactor(double scale_factor);
    absl::optional<ScaledTimerImpl*> pop(MonotonicTime now);
    absl::optional<MonotonicTime> firstScheduleTime() const;

  private:
    struct Compare {
      MonotonicTime targetTime(const ScaledTimerImpl* timer) const;
      bool operator()(const ScaledTimerImpl* lhs, const ScaledTimerImpl* rhs) const;
      double scale_factor;
    };

    std::set<ScaledTimerImpl*, Compare> timers_;
  };

  void scheduleTimer(ScaledTimerImpl* tracker);
  void descheduleTimer(ScaledTimerImpl* tracker);

  /**
   * Called by the internal timer when the next tracked timer is ready to be triggered.
   */
  void dispatchCallback();
  /**
   * Internal helper method that updates `dispatch_timer_` based on the current scale value.
   */
  void internalScheduleTimer();

  Dispatcher& dispatcher_;
  const TimerPtr dispatch_timer_;
  ScaledTimerList timers_;
};

} // namespace Event
} // namespace Envoy