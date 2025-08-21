#include "source/common/event/timer_impl.h"

#include <chrono>

#include "source/common/common/assert.h"

#include "event2/event.h"

namespace Envoy {
namespace Event {

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

void TimerImpl::disableTimer() {
  ASSERT(dispatcher_.isThreadSafe());
  event_del(&raw_event_);
}

void TimerImpl::enableTimer(const std::chrono::milliseconds d, const ScopeTrackedObject* object) {
  timeval tv;
  TimerUtils::durationToTimeval(d, tv);
  internalEnableTimer(tv, object);
}

void TimerImpl::enableHRTimer(const std::chrono::microseconds d,
                              const ScopeTrackedObject* object = nullptr) {
  timeval tv;
  TimerUtils::durationToTimeval(d, tv);
  internalEnableTimer(tv, object);
}

void TimerImpl::internalEnableTimer(const timeval& tv, const ScopeTrackedObject* object) {
  ASSERT(dispatcher_.isThreadSafe());
  object_ = object;

  event_add(&raw_event_, &tv);
}

bool TimerImpl::enabled() {
  ASSERT(dispatcher_.isThreadSafe());
  return 0 != evtimer_pending(&raw_event_, nullptr);
}

} // namespace Event
} // namespace Envoy
