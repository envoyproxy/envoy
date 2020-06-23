#include "common/event/schedulable_cb_impl.h"

#include "common/common/assert.h"

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
  event_active(&raw_event_, EV_TIMEOUT, 0);
}

void SchedulableCallbackImpl::cancel() { event_del(&raw_event_); }

bool SchedulableCallbackImpl::enabled() { return 0 != evtimer_pending(&raw_event_, nullptr); }

} // namespace Event
} // namespace Envoy
