#pragma once

#include "envoy/event/signal.h"
#include "envoy/network/io_handle.h"

#include "common/api/os_sys_calls_impl.h"
#include "common/event/dispatcher_impl.h"
#include "common/event/event_impl_base.h"
#include "common/network/io_socket_handle_impl.h"
#include "common/singleton/threadsafe_singleton.h"

#include "absl/container/flat_hash_map.h"

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
  Network::IoHandlePtr read_handle_;
};

using eventBridgeHandlersSingleton =
    ThreadSafeSingleton<absl::flat_hash_map<signal_t, std::shared_ptr<Network::IoHandle>>>;
} // namespace Event
} // namespace Envoy
