#include "common/network/listen_socket_impl.h"

#include <sys/socket.h>
#include <sys/types.h>

#include <string>

#include "envoy/common/exception.h"

#include "common/common/assert.h"
#include "common/common/fmt.h"
#include "common/network/address_impl.h"
#include "common/network/utility.h"

namespace Envoy {
namespace Network {

void ListenSocketImpl::doBind() {
  int rc = local_address_->bind(fd_);
  if (rc == -1) {
    close();
    throw EnvoyException(
        fmt::format("cannot bind '{}': {}", local_address_->asString(), strerror(errno)));
  }
  if (local_address_->type() == Address::Type::Ip && local_address_->ip()->port() == 0) {
    // If the port we bind is zero, then the OS will pick a free port for us (assuming there are
    // any), and we need to find out the port number that the OS picked.
    local_address_ = Address::addressFromFd(fd_);
  }
}

void ListenSocketImpl::setListenSocketOptions(const Network::Socket::OptionsSharedPtr& options) {
  if (options) {
    for (const auto& option : *options) {
      if (!option->setOption(*this, SocketState::PreBind)) {
        throw EnvoyException("ListenSocket: Setting socket options failed");
      }
    }
  }
}

TcpListenSocket::TcpListenSocket(const Address::InstanceConstSharedPtr& address,
                                 const Network::Socket::OptionsSharedPtr& options,
                                 bool bind_to_port)
    : ListenSocketImpl(address->socket(Address::SocketType::Stream), address) {
  RELEASE_ASSERT(fd_ != -1);

  int on = 1;
  int rc = setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
  RELEASE_ASSERT(rc != -1);

  setListenSocketOptions(options);

  if (bind_to_port) {
    doBind();
  }
}

TcpListenSocket::TcpListenSocket(int fd, const Address::InstanceConstSharedPtr& address,
                                 const Network::Socket::OptionsSharedPtr& options)
    : ListenSocketImpl(fd, address) {
  setListenSocketOptions(options);
}

UdsListenSocket::UdsListenSocket(const Address::InstanceConstSharedPtr& address)
    : ListenSocketImpl(address->socket(Address::SocketType::Stream), address) {
  RELEASE_ASSERT(fd_ != -1);
  doBind();
}

UdsListenSocket::UdsListenSocket(int fd, const Address::InstanceConstSharedPtr& address)
    : ListenSocketImpl(fd, address) {}

} // namespace Network
} // namespace Envoy
