#pragma once

#include "envoy/network/listener.h"

#include "source/common/event/dispatcher_impl.h"
#include "source/common/network/listen_socket_impl.h"

namespace Envoy {
namespace Network {

/**
 * Base libevent implementation of Network::Listener.
 */
class BaseListenerImpl : public virtual Listener {
public:
  /**
   * @param socket the listening socket for this listener. It might be shared
   * with other listeners if all listeners use single listen socket.
   */
  BaseListenerImpl(Event::Dispatcher& dispatcher, SocketSharedPtr socket);

protected:
  Address::InstanceConstSharedPtr local_address_;
  Event::Dispatcher& dispatcher_;
  const SocketSharedPtr socket_;
};

} // namespace Network
} // namespace Envoy
