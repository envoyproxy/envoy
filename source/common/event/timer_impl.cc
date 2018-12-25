#include "common/event/timer_impl.h"

#include <chrono>

#include "common/common/assert.h"

#include "event2/event.h"

namespace Envoy {
namespace Event {

TimerImpl::TimerImpl(Libevent::BasePtr& libevent, TimerCb cb) : cb_(cb) {
  ASSERT(cb_);
  evtimer_assign(
      &raw_event_, libevent.get(),
      [](evutil_socket_t, short, void* arg) -> void { static_cast<TimerImpl*>(arg)->cb_(); }, this);
}

void TimerImpl::disableTimer() { event_del(&raw_event_); }

void TimerImpl::enableTimer(const std::chrono::milliseconds& d) {
  if (d.count() == 0) {
    event_active(&raw_event_, EV_TIMEOUT, 0);
  } else {
    auto sec = std::chrono::duration_cast<std::chrono::seconds>(d);
    auto usec = std::chrono::duration_cast<std::chrono::microseconds>(d - sec);

    timeval tv;
    tv.tv_sec = sec.count();
    tv.tv_usec = usec.count();

    event_add(&raw_event_, &tv);
  }
}

} // namespace Event
} // namespace Envoy
