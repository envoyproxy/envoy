#include "common/network/listener_impl.h"

#include <sys/un.h>

#include "envoy/common/exception.h"

#include "common/common/assert.h"
#include "common/common/empty_string.h"
#include "common/common/fmt.h"
#include "common/event/dispatcher_impl.h"
#include "common/event/file_event_impl.h"
#include "common/network/address_impl.h"

namespace Envoy {
namespace Network {

Address::InstanceConstSharedPtr ListenerImpl::getLocalAddress(int fd) {
  return Address::addressFromFd(fd);
}

void ListenerImpl::listenCallback(int fd, const sockaddr& remote_addr, socklen_t remote_addr_len) {
  // Get the local address from the new socket if the listener is listening on IP ANY
  // (e.g., 0.0.0.0 for IPv4) (local_address_ is nullptr in this case).
  const Address::InstanceConstSharedPtr& local_address =
      local_address_ ? local_address_ : getLocalAddress(fd);
  // The accept() call that filled in remote_addr doesn't fill in more than the sa_family field
  // for Unix domain sockets; apparently there isn't a mechanism in the kernel to get the
  // sockaddr_un associated with the client socket when starting from the server socket.
  // We work around this by using our own name for the socket in this case.
  // Pass the 'v6only' parameter as true if the local_address is an IPv6 address. This has no effect
  // if the socket is a v4 socket, but for v6 sockets this will create an IPv4 remote address if an
  // IPv4 local_address was created from an IPv6 mapped IPv4 address.
  const Address::InstanceConstSharedPtr& remote_address =
      (remote_addr.sa_family == AF_UNIX)
          ? Address::peerAddressFromFd(fd)
          : Address::addressFromSockAddr(*reinterpret_cast<const sockaddr_storage*>(&remote_addr),
                                         remote_addr_len,
                                         local_address->ip()->version() == Address::IpVersion::v6);
  cb_.onAccept(std::make_unique<AcceptedSocketImpl>(fd, local_address, remote_address),
               hand_off_restored_destination_connections_);
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
    const int ret = ::listen(socket.fd(), 128);

    if (ret == -1) {
      throw CreateListenerException(
          fmt::format("cannot listen on socket: {}", socket.localAddress()->asString()));
    }

    file_event_ = dispatcher.createFileEvent(
        socket.fd(),
        [this, socket_fd = socket.fd()](uint32_t) {
          while (true) {
            sockaddr_storage remote_addr;
            socklen_t remote_addr_len = sizeof(remote_addr);
            const int ret = ::accept4(socket_fd, reinterpret_cast<sockaddr*>(&remote_addr),
                                      &remote_addr_len, O_NONBLOCK);
            if (ret < 0) {
              if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK ||
                  errno == ECONNABORTED) {
                return;
              }
              PANIC(fmt::format("listener accept failure: {}", strerror(errno)));
            }
            listenCallback(ret, reinterpret_cast<sockaddr&>(remote_addr), remote_addr_len);
          }
        },
        Event::FileTriggerType::Level, Event::FileReadyType::Read);

    if (!Network::Socket::applyOptions(socket.options(), socket,
                                       envoy::api::v2::core::SocketOption::STATE_LISTENING)) {
      throw CreateListenerException(fmt::format(
          "cannot set post-listen socket option on socket: {}", socket.localAddress()->asString()));
    }
  }
}

void ListenerImpl::enable() {
  if (file_event_ != nullptr) {
    file_event_->setEnabled(Event::FileReadyType::Read);
  }
}

void ListenerImpl::disable() {
  if (file_event_ != nullptr) {
    file_event_->setEnabled(0);
  }
}

} // namespace Network
} // namespace Envoy
