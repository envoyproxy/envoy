#include "common/event/scaled_timer_impl.h"
#include <chrono>

namespace Envoy {
namespace Event {
namespace {

void resetTimer(Timer& timer, const ScopeTrackedObject* scope, std::chrono::milliseconds d) {
  timer.enableTimer(d, scope);
}

void resetTimer(Timer& timer, const ScopeTrackedObject* scope, std::chrono::microseconds d) {
  timer.enableHRTimer(d, scope);
}

} // namespace

ScaledTimerImpl::ScaledTimerImpl(Dispatcher& dispatcher, TimerCb cb, double scale_factor)
    : scale_factor_(scale_factor > 1      ? 1
                    : !(scale_factor > 0) ? 0
                                          : scale_factor),
      timer_(dispatcher.createTimer(std::move(cb))), time_source_(dispatcher.timeSource()) {}

void ScaledTimerImpl::enableTimer(const std::chrono::milliseconds& min,
                                  const std::chrono::milliseconds& max,
                                  const ScopeTrackedObject* scope) {
  ASSERT(max >= min);
  enabled_.last_enabled = time_source_.monotonicTime();
  enabled_.interval = Enabled::Interval<std::chrono::milliseconds>(min, max);
  enabled_.scope = scope;
  timer_->enableTimer(
      std::chrono::duration_cast<std::chrono::milliseconds>(scale_factor_ * (max - min)) + min,
      scope);
}

void ScaledTimerImpl::enableHRTimer(const std::chrono::microseconds& min,
                                    const std::chrono::microseconds& max,
                                    const ScopeTrackedObject* scope) {
  ASSERT(max >= min);
  enabled_.last_enabled = time_source_.monotonicTime();
  enabled_.interval = Enabled::Interval<std::chrono::microseconds>(min, max);
  enabled_.scope = scope;
  timer_->enableHRTimer(
      std::chrono::duration_cast<std::chrono::microseconds>(scale_factor_ * (max - min)) + min,
      scope);
}

void ScaledTimerImpl::disableTimer() { timer_->disableTimer(); }

bool ScaledTimerImpl::enabled() { return timer_->enabled(); }

void ScaledTimerImpl::setScaleFactor(double scale_factor) {
  scale_factor_ = scale_factor > 1 ? 1 : !(scale_factor > 0) ? 0 : scale_factor;

  if (!timer_->enabled()) {
    return;
  }

  auto visitor = [this](auto& interval) -> void {
    using T = typename std::remove_reference_t<decltype(interval)>::value_type;

    const auto trigger_time =
        std::chrono::duration_cast<T>(scale_factor_ * (interval.max - interval.min)) +
        interval.min + enabled_.last_enabled;

    const T delay = std::chrono::duration_cast<T>(trigger_time - time_source_.monotonicTime());
    resetTimer(*timer_, enabled_.scope, std::max(T::zero(), delay));
  };

  absl::visit(visitor, enabled_.interval);
}

ScaledTimerImpl::Enabled::Enabled()
    : interval(Enabled::Interval<std::chrono::milliseconds>(std::chrono::milliseconds::zero(),
                                                            std::chrono::milliseconds::zero())) {}

} // namespace Event
} // namespace Envoy