#include "source/common/event/schedulable_cb_impl.h"

#include "source/common/common/assert.h"

#include "event2/event.h"

namespace Envoy {
namespace Event {

SchedulableCallbackImpl::SchedulableCallbackImpl(Libevent::BasePtr& libevent,
                                                 std::function<void()> cb)
    : cb_(cb) {
  ASSERT(cb_);
  evtimer_assign(
      &raw_event_, libevent.get(),
      [](evutil_socket_t, short, void* arg) -> void {
        SchedulableCallbackImpl* cb = static_cast<SchedulableCallbackImpl*>(arg);
        cb->cb_();
      },
      this);
}

void SchedulableCallbackImpl::scheduleCallbackCurrentIteration() {
  if (enabled()) {
    return;
  }
  // event_active directly adds the event to the end of the work queue so it executes in the current
  // iteration of the event loop.
  event_active(&raw_event_, EV_TIMEOUT, 0);
}

void SchedulableCallbackImpl::scheduleCallbackNextIteration() {
  if (enabled()) {
    return;
  }
  // libevent computes the list of timers to move to the work list after polling for fd events, but
  // iteration through the work list starts. Zero delay timers added while iterating through the
  // work list execute on the next iteration of the event loop.
  const timeval zero_tv{};
  event_add(&raw_event_, &zero_tv);
}

void SchedulableCallbackImpl::cancel() { event_del(&raw_event_); }

bool SchedulableCallbackImpl::enabled() { return 0 != evtimer_pending(&raw_event_, nullptr); }

} // namespace Event
} // namespace Envoy
