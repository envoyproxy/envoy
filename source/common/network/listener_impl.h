#pragma once

#include <vcl/vppcom.h>
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
  Address::InstanceConstSharedPtr remote_address_; //alagalah - Remove this and do a get_attr remote
  ListenerCallbacks& cb_;
  const bool hand_off_restored_destination_connections_;
  struct event *ev_;
  int fd_;

private:
  static void errorCallback(void* context);
  static void listenCallback(evconnlistener*, evutil_socket_t fd, sockaddr* remote_addr, int remote_addr_len, void* arg);
  static void evListenCallback(evutil_socket_t fd, short what, void* arg);
  static void vclListenCallback(uint32_t new_session_index, vppcom_endpt_t *ep, void* arg);
};

} // namespace Network
} // namespace Envoy
