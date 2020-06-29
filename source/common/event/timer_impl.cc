#include "common/event/timer_impl.h"

#include <chrono>

#include "common/common/assert.h"

#include "event2/event.h"

namespace Envoy {
namespace Event {
namespace {

void resetTimer(TimerImpl& timer, std::chrono::milliseconds d) {
  timer.enableTimer(d, timer.scope());
}

void resetTimer(TimerImpl& timer, std::chrono::microseconds d) {
  timer.enableHRTimer(d, timer.scope());
}

} // namespace

TimerImpl::TimerImpl(Libevent::BasePtr& libevent, TimerCb cb, Dispatcher& dispatcher)
    : cb_(cb), dispatcher_(dispatcher) {
  ASSERT(cb_);
  evtimer_assign(
      &raw_event_, libevent.get(),
      [](evutil_socket_t, short, void* arg) -> void {
        TimerImpl* timer = static_cast<TimerImpl*>(arg);
        if (timer->object_ == nullptr) {
          timer->cb_();
          return;
        }
        ScopeTrackerScopeState scope(timer->object_, timer->dispatcher_);
        timer->object_ = nullptr;
        timer->cb_();
      },
      this);
}

void TimerImpl::disableTimer() { event_del(&raw_event_); }

void TimerImpl::enableTimer(const std::chrono::milliseconds& d, const ScopeTrackedObject* object) {
  timeval tv;
  TimerUtils::durationToTimeval(d, tv);
  internalEnableTimer(tv, object);
}

void TimerImpl::enableHRTimer(const std::chrono::microseconds& d,
                              const ScopeTrackedObject* object = nullptr) {
  timeval tv;
  TimerUtils::durationToTimeval(d, tv);
  internalEnableTimer(tv, object);
}

void TimerImpl::internalEnableTimer(const timeval& tv, const ScopeTrackedObject* object) {
  object_ = object;
  if (tv.tv_sec == 0 && tv.tv_usec == 0) {
    event_active(&raw_event_, EV_TIMEOUT, 0);
  } else {
    event_add(&raw_event_, &tv);
  }
}

bool TimerImpl::enabled() { return 0 != evtimer_pending(&raw_event_, nullptr); }

ScaledTimerImpl::ScaledTimerImpl(Libevent::BasePtr& libevent, TimerCb cb,
                                 Event::Dispatcher& dispatcher, double scale_factor)
    : scale_factor_(scale_factor > 1 ? 1 : !(scale_factor > 0) ? 0 : scale_factor),
      timer_(libevent, cb, dispatcher) {}

void ScaledTimerImpl::enableTimer(const std::chrono::milliseconds& min,
                                  const std::chrono::milliseconds& max,
                                  const ScopeTrackedObject* scope) {
  ASSERT(max >= min);
  enabled_.last_enabled = timer_.dispatcher().timeSource().monotonicTime();
  enabled_.interval = Enabled::Interval<std::chrono::milliseconds>(min, max);
  timer_.enableTimer(
      std::chrono::duration_cast<std::chrono::milliseconds>(scale_factor_ * (max - min)) + min,
      scope);
}

void ScaledTimerImpl::enableHRTimer(const std::chrono::microseconds& min,
                                    const std::chrono::microseconds& max,
                                    const ScopeTrackedObject* scope) {
  ASSERT(max >= min);
  enabled_.last_enabled = timer_.dispatcher().timeSource().monotonicTime();
  enabled_.interval = Enabled::Interval<std::chrono::microseconds>(min, max);
  timer_.enableHRTimer(
      std::chrono::duration_cast<std::chrono::microseconds>(scale_factor_ * (max - min)) + min,
      scope);
}

void ScaledTimerImpl::disableTimer() { timer_.disableTimer(); }

bool ScaledTimerImpl::enabled() { return timer_.enabled(); }

void ScaledTimerImpl::setScaleFactor(double scale_factor) {
  scale_factor_ = scale_factor > 1 ? 1 : !(scale_factor > 0) ? 0 : scale_factor;

  if (!timer_.enabled()) {
    return;
  }

  auto visitor = [this](auto& interval) -> void {
    using T = typename std::remove_reference_t<decltype(interval)>::value_type;

    const auto trigger_time =
        std::chrono::duration_cast<T>(scale_factor_ * (interval.max - interval.min)) +
        interval.min + enabled_.last_enabled;

    const T delay = std::chrono::duration_cast<T>(trigger_time -
                                                  timer_.dispatcher().timeSource().monotonicTime());
    resetTimer(timer_, std::max(T::zero(), delay));
  };

  absl::visit(visitor, enabled_.interval);
}

} // namespace Event
} // namespace Envoy
