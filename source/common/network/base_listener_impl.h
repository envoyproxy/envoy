#pragma once

#include "envoy/network/listener.h"

#include "common/event/dispatcher_impl.h"
#include "common/network/listen_socket_impl.h"

namespace Envoy {
namespace Network {

/**
 * Base libevent implementation of Network::Listener.
 */
class BaseListenerImpl : public virtual Listener {
public:
  /**
   * @param socket the listening socket for this listener. It might be shared
   * with other listeners if all listeners use single listen socket. It could be nullptr for
   * internal listener.
   */
  BaseListenerImpl(Event::DispatcherImpl& dispatcher, SocketSharedPtr socket,
                   const Address::InstanceConstSharedPtr& local_address = nullptr);

protected:
  Address::InstanceConstSharedPtr local_address_;
  Event::DispatcherImpl& dispatcher_;
  const SocketSharedPtr socket_;
};

} // namespace Network
} // namespace Envoy
