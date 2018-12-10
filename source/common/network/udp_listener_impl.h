#pragma once

#include "base_listener_impl.h"

namespace Envoy {
namespace Network {

/**
 * libevent implementation of Network::Listener for UDP.
 */
class UdpListenerImpl : public BaseListenerImpl {
public:
  UdpListenerImpl(const Event::DispatcherImpl& dispatcher, Socket& socket, UdpListenerCallbacks& cb,
                  bool bind_to_port);

protected:
  void setupServerSocket(const Event::DispatcherImpl& dispatcher, Socket& socket) override;

  UdpListenerCallbacks& cb_;
};

} // namespace Network
} // namespace Envoy
