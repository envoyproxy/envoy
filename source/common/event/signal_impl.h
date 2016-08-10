#pragma once

#include "event_impl_base.h"

#include "envoy/event/signal.h"

namespace Event {

/**
 * libevent implementation of Event::SignalEvent.
 */
class SignalEventImpl : public SignalEvent, ImplBase {
public:
  SignalEventImpl(DispatcherImpl& dispatcher, int signal_num, SignalCb cb);

private:
  SignalCb cb_;
};

} // Event
