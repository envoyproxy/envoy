#pragma once

#include "envoy/api/os_sys_calls.h"
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

  void disable();
  void enable();

protected:
  virtual Address::InstanceConstSharedPtr getLocalAddress(int fd);

  Address::InstanceConstSharedPtr local_address_;
  ListenerCallbacks& cb_;
  const bool hand_off_restored_destination_connections_;

private:
  void listenCallback(int fd, const sockaddr& remote_addr, socklen_t remote_addr_len);

  Api::OsSysCalls& os_sys_calls_;
  Event::FileEventPtr file_event_;
};

} // namespace Network
} // namespace Envoy
