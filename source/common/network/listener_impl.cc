#include "common/network/listener_impl.h"

#include <netinet/tcp.h>
#include <sys/un.h>

#include "envoy/common/exception.h"

#include "common/common/assert.h"
#include "common/common/empty_string.h"
#include "common/common/fmt.h"
#include "common/event/dispatcher_impl.h"
#include "common/event/file_event_impl.h"
#include "common/network/address_impl.h"

#include "event2/listener.h"

// On macOS the socket MUST be listening already for TCP_FASTOPEN to be set and backlog MUST be 1
// (the actual value is set via the net.inet.tcp.fastopen_backlog kernel parameter.
// For Linux we default to 128, which libevent is using in
// https://github.com/libevent/libevent/blob/release-2.1.8-stable/listener.c#L176
#if defined(__APPLE__)
#define TFO_BACKLOG 1
#else
#define TFO_BACKLOG 128
#endif

namespace Envoy {
namespace Network {

Address::InstanceConstSharedPtr ListenerImpl::getLocalAddress(int fd) {
  return Address::addressFromFd(fd);
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
                           bool bind_to_port, bool enable_tcp_fast_open,
                           bool hand_off_restored_destination_connections)
    : local_address_(nullptr), cb_(cb),
      hand_off_restored_destination_connections_(hand_off_restored_destination_connections),
      listener_(nullptr) {
  const auto ip = socket.localAddress()->ip();

  // Only use the listen socket's local address for new connections if it is not the all hosts
  // address (e.g., 0.0.0.0 for IPv4).
  if (!(ip && ip->isAnyAddress())) {
    local_address_ = socket.localAddress();
  }

  if (bind_to_port) {
    listener_.reset(
        evconnlistener_new(&dispatcher.base(), listenCallback, this, 0, -1, socket.fd()));

    if (!listener_) {
      throw CreateListenerException(
          fmt::format("cannot listen on socket: {}", socket.localAddress()->asString()));
    }

    // TODO: cleanup redundant setsockopt logic
    if (enable_tcp_fast_open) {
      int backlog = TFO_BACKLOG;
      int fd = socket.fd();
      int rc = setsockopt(fd, IPPROTO_TCP, TCP_FASTOPEN, &backlog, sizeof(backlog));
      if (rc == -1) {
        if (errno == ENOPROTOOPT) {
          throw EnvoyException(fmt::format(
              "TCP Fast Open is unsupported on listening socket fd {} : {}", fd, strerror(errno)));
        } else {
          throw EnvoyException(
              fmt::format("Failed to enable TCP Fast Open on listening socket fd {} : {}", fd,
                          strerror(errno)));
        }
      } else {
        ENVOY_LOG_MISC(debug, "Enabled TFO on listening socket fd {}.", fd);
      }
    } else {
      int curbacklog;
      int fd = socket.fd();
      socklen_t optlen = sizeof(curbacklog);
      int rc = getsockopt(fd, IPPROTO_TCP, TCP_FASTOPEN, &curbacklog, &optlen);
      // curbacklog == 0 means TFO is supported and already disabled
      if (rc != -1 && curbacklog != 0) {
        int backlog = 0;
        int rc = setsockopt(fd, IPPROTO_TCP, TCP_FASTOPEN, &backlog, sizeof(backlog));
        if (rc == -1) {
          if (errno == ENOPROTOOPT) {
            throw EnvoyException(
                fmt::format("TCP Fast Open is unsupported on listening socket fd {} : {}", fd,
                            strerror(errno)));
          } else {
            throw EnvoyException(
                fmt::format("Failed to disable TCP Fast Open on listening socket fd {} : {}", fd,
                            strerror(errno)));
          }
        } else {
          ENVOY_LOG_MISC(debug, "Disabled TFO on listening socket fd {}.", fd);
        }
      }
    }

    evconnlistener_set_error_cb(listener_.get(), errorCallback);
  }
}

void ListenerImpl::errorCallback(evconnlistener*, void*) {
  // We should never get an error callback. This can happen if we run out of FDs or memory. In those
  // cases just crash.
  PANIC(fmt::format("listener accept failure: {}", strerror(errno)));
}

} // namespace Network
} // namespace Envoy
