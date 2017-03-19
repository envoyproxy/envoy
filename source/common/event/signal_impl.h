#pragma once

#include "envoy/event/signal.h"

#include "common/event/event_impl_base.h"

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
