#include "common/event/signal_impl.h"

#include "common/event/dispatcher_impl.h"

#include "event2/event.h"

namespace Envoy {
namespace Event {

SignalEventImpl::SignalEventImpl(DispatcherImpl& dispatcher, int signal_num, SignalCb cb)
    : cb_(cb) {
  evsignal_assign(
      &raw_event_, &dispatcher.base(), signal_num,
      [](evutil_socket_t, short, void* arg) -> void { static_cast<SignalEventImpl*>(arg)->cb_(); },
      this);
  evsignal_add(&raw_event_, nullptr);
}

} // namespace Event
} // namespace Envoy
