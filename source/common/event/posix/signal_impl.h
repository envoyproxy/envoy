#pragma once

#include "envoy/event/signal.h"

#include "source/common/event/dispatcher_impl.h"
#include "source/common/event/event_impl_base.h"

namespace Envoy {
namespace Event {

/**
 * libevent implementation of Event::SignalEvent.
 */
class SignalEventImpl : public SignalEvent, ImplBase {
public:
  SignalEventImpl(DispatcherImpl& dispatcher, signal_t signal_num, SignalCb cb);

private:
  SignalCb cb_;
};
} // namespace Event
} // namespace Envoy
