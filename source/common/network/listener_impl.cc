#include "common/network/listener_impl.h"

#include <sys/un.h>

#include "envoy/common/exception.h"

#include "common/common/assert.h"
#include "common/common/empty_string.h"
#include "common/common/fmt.h"
#include "common/event/dispatcher_impl.h"
#include "common/event/file_event_impl.h"
#include "common/network/address_impl.h"

#include "event2/listener.h"

namespace Envoy {
namespace Network {

Address::InstanceConstSharedPtr ListenerImpl::getLocalAddress(int fd) {
  return Address::addressFromFd(fd);
}


/*
 * 1. Register an event with libevent and get "ev" back (struct event)
 *    struct event *ev = event_new (&dispatcher.base(), -1, EV_READ | EV_PERSIST, listenCallback, this)
 * 2. call vppcom_session_register_listener (.... ) but instead of listenCallback, call vppcomListenCallback()
 * 3. vppcomListenCallback(u32 new_session, vppcom_endpt_t *ep, "void*" this);
 * 4. Extend
 *
 */
void ListenerImpl::vclListenCallback(uint32_t new_session, vppcom_endpt_t *ep,
                                  void* arg) {
  ListenerImpl* listener = static_cast<ListenerImpl*>(arg);
  // Get the local address from the new socket if the listener is listening on IP ANY
  // (e.g., 0.0.0.0 for IPv4) (local_address_ is nullptr in this case).
  const Address::InstanceConstSharedPtr& local_address =
      listener->local_address_ ? listener->local_address_ :
      listener->getLocalAddress(new_session);

  listener->remote_address_ = Address::addressFromSockAddr(reinterpret_cast<const vppcom_endpt_t &>(ep));
  listener->fd_ = new_session;
  event_active (listener->ev_, EV_READ, 0);
}

void ListenerImpl::evListenCallback(evutil_socket_t, short, void* arg) {
  ListenerImpl* listener = static_cast<ListenerImpl*>(arg);
  listener->cb_.onAccept(std::make_unique<AcceptedSocketImpl>(listener->fd_, listener->local_address_, listener->remote_address_),
                         listener->hand_off_restored_destination_connections_);
}

void ListenerImpl::listenCallback(evconnlistener*, evutil_socket_t fd, sockaddr* remote_addr,
                                  int remote_addr_len, void* arg) {
  ListenerImpl* listener = static_cast<ListenerImpl*>(arg);
  // Get the local address from the new socket if the listener is listening on IP ANY
  // (e.g., 0.0.0.0 for IPv4) (local_address_ is nullptr in this case).
  const Address::InstanceConstSharedPtr& local_address =
      listener->local_address_ ? listener->local_address_ : listener->getLocalAddress(fd);
  // The accept() call that filled in remote_addr doesn't fill in more than the sa_family field
  // for Unix domain sockets; apparently there isn't a mechanism in the kernel to get the
  // sockaddr_un associated with the client socket when starting from the server socket.
  // We work around this by using our own name for the socket in this case.
  // Pass the 'v6only' parameter as true if the local_address is an IPv6 address. This has no effect
  // if the socket is a v4 socket, but for v6 sockets this will create an IPv4 remote address if an
  // IPv4 local_address was created from an IPv6 mapped IPv4 address.
  const Address::InstanceConstSharedPtr& remote_address =
      (remote_addr->sa_family == AF_UNIX)
          ? Address::peerAddressFromFd(fd)
          : Address::addressFromSockAddr(*reinterpret_cast<const sockaddr_storage*>(remote_addr),
                                         remote_addr_len,
                                         local_address->ip()->version() == Address::IpVersion::v6);
  listener->cb_.onAccept(std::make_unique<AcceptedSocketImpl>(fd, local_address, remote_address),
                         listener->hand_off_restored_destination_connections_);
}

ListenerImpl::ListenerImpl(Event::DispatcherImpl& dispatcher, Socket& socket, ListenerCallbacks& cb,
                           bool bind_to_port, bool hand_off_restored_destination_connections)
    : local_address_(nullptr), cb_(cb),
      hand_off_restored_destination_connections_(hand_off_restored_destination_connections) {
  const auto ip = socket.localAddress()->ip();

  // Only use the listen socket's local address for new connections if it is not the all hosts
  // address (e.g., 0.0.0.0 for IPv4).
  if (!(ip && ip->isAnyAddress())) {
    local_address_ = socket.localAddress();
  }

  if (bind_to_port) {
    ev_ = event_new (&dispatcher.base(), -1, EV_READ | EV_PERSIST, evListenCallback, this);
    event_add (ev_, NULL);
    auto rv = vppcom_session_register_listener (socket.fd(), vclListenCallback,
                                                errorCallback, 0 /* flags */,
                                                0 /* listen queue depth */,
                                                this);
    if (rv) {
      throw CreateListenerException(
          fmt::format("cannot listen on socket: {}", socket.localAddress()->asString()));
    }

    Socket::OptionsSharedPtr options = socket.options();
    if (options != nullptr) {
      for (auto& option : *options) {
        if (!option->setOption(socket, Socket::SocketState::Listening)) {
          throw CreateListenerException(
              fmt::format("cannot set post-listen socket option on socket: {}",
                          socket.localAddress()->asString()));
        }
      }
    }
  }
}

void ListenerImpl::errorCallback(void*) {
  // We should never get an error callback. This can happen if we run out of FDs or memory. In those
  // cases just crash.
  PANIC(fmt::format("listener accept failure: {}", strerror(errno)));
}

} // namespace Network
} // namespace Envoy
