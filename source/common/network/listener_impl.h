#pragma once

#include "envoy/network/listener.h"

#include "common/event/dispatcher_impl.h"
#include "common/event/libevent.h"
#include "common/network/listen_socket_impl.h"

#include "event2/event.h"

namespace Envoy {
namespace Network {

/**
 * libevent implementation of Network::Listener.
 */
class ListenerImpl : public Listener {
public:
  ListenerImpl(Event::DispatcherImpl& dispatcher, Socket& socket, ListenerCallbacks& cb,
               bool bind_to_port, bool hand_off_restored_destination_connections);

protected:
  virtual Address::InstanceConstSharedPtr getLocalAddress(int fd);

  Address::InstanceConstSharedPtr local_address_;
  ListenerCallbacks& cb_;
  const bool hand_off_restored_destination_connections_;

private:
  static void errorCallback(evconnlistener* listener, void* context);
  static void listenCallback(evconnlistener*, evutil_socket_t fd, sockaddr* remote_addr,
                             int remote_addr_len, void* arg);

  Event::Libevent::ListenerPtr listener_;
};

} // namespace Network
} // namespace Envoy
