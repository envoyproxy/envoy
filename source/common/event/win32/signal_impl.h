#pragma once

#include "envoy/event/signal.h"
#include "envoy/network/io_handle.h"

#include "source/common/event/dispatcher_impl.h"
#include "source/common/event/event_impl_base.h"
#include "source/common/network/io_socket_handle_impl.h"
#include "source/common/singleton/threadsafe_singleton.h"

#include "absl/container/flat_hash_map.h"

namespace Envoy {
namespace Event {

/**
 * libevent implementation of Event::SignalEvent.
 */
class SignalEventImpl : public SignalEvent {
public:
  SignalEventImpl(DispatcherImpl& dispatcher, signal_t signal_num, SignalCb cb);

private:
  SignalCb cb_;
  Network::IoHandlePtr read_handle_;
};

// Windows ConsoleControlHandler does not allow for a context. As a result the thread
// spawned to handle the console events communicates with the main program with this socketpair.
// Here we have a map from signal types to IoHandle. When we write to this handle we trigger an
// event that notifies Envoy to act on the signal.
using eventBridgeHandlersSingleton =
    ThreadSafeSingleton<std::array<std::shared_ptr<Network::IoHandle>, ENVOY_WIN32_SIGNAL_COUNT>>;
} // namespace Event
} // namespace Envoy
