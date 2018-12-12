#pragma once

#include "envoy/network/listener.h"

#include "common/event/dispatcher_impl.h"
#include "common/event/libevent.h"
#include "common/network/listen_socket_impl.h"

#include "event2/event.h"

namespace Envoy {
namespace Network {

/**
 * Base libevent implementation of Network::Listener.
 */
class BaseListenerImpl : public Listener {
public:
  BaseListenerImpl(const Event::DispatcherImpl& dispatcher, Socket& socket);

  virtual void disable() PURE;
  virtual void enable() PURE;

protected:
  virtual Address::InstanceConstSharedPtr getLocalAddress(int fd);

  Address::InstanceConstSharedPtr local_address_;
  const Event::DispatcherImpl& dispatcher_;
  Socket& socket_;
};

} // namespace Network
} // namespace Envoy
