#include "common/event/timer_impl.h"

#include <chrono>

#include "common/common/assert.h"
#include "common/event/dispatcher_impl.h"

#include "event2/event.h"

namespace Envoy {
namespace Event {

TimerImpl::TimerImpl(DispatcherImpl& dispatcher, TimerCb cb) : cb_(cb) {
  ASSERT(cb_);
  evtimer_assign(
      &raw_event_, &dispatcher.base(),
      [](evutil_socket_t, short, void* arg) -> void { static_cast<TimerImpl*>(arg)->cb_(); }, this);
}

void TimerImpl::disableTimer() { event_del(&raw_event_); }

void TimerImpl::enableTimer(const std::chrono::milliseconds& d) {
  if (d.count() == 0) {
    event_active(&raw_event_, EV_TIMEOUT, 0);
  } else {
    std::chrono::microseconds us = std::chrono::duration_cast<std::chrono::microseconds>(d);
    timeval tv;
    tv.tv_sec = us.count() / 1000000;
    tv.tv_usec = us.count() % 1000000;
    event_add(&raw_event_, &tv);
  }
}

} // namespace Event
} // namespace Envoy
