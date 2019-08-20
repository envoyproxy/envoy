#include "common/event/timer_impl.h"

#include <chrono>

#include "common/common/assert.h"

#include "event2/event.h"

namespace Envoy {
namespace Event {

void TimerUtils::millisecondsToTimeval(const std::chrono::milliseconds& d, timeval& tv) {
  std::chrono::seconds secs = std::chrono::duration_cast<std::chrono::seconds>(d);
  std::chrono::microseconds usecs = std::chrono::duration_cast<std::chrono::microseconds>(d - secs);

  tv.tv_sec = secs.count();
  tv.tv_usec = usecs.count();
}

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
  object_ = object;
  if (d.count() == 0) {
    event_active(&raw_event_, EV_TIMEOUT, 0);
  } else {
    timeval tv;
    TimerUtils::millisecondsToTimeval(d, tv);
    event_add(&raw_event_, &tv);
  }
}

bool TimerImpl::enabled() { return 0 != evtimer_pending(&raw_event_, nullptr); }

} // namespace Event
} // namespace Envoy
