#include "listen_socket_impl.h"
#include "utility.h"

#include "envoy/common/exception.h"

#include "common/common/assert.h"
#include "common/network/address_impl.h"

namespace Network {

void ListenSocketImpl::doBind() {
  int rc = local_address_->bind(fd_);
  if (rc == -1) {
    close();
    throw EnvoyException(
        fmt::format("cannot bind '{}': {}", local_address_->asString(), strerror(errno)));
  }
}

TcpListenSocket::TcpListenSocket(uint32_t port, bool bind_to_port) {
  // TODO: IPv6 support.
  local_address_.reset(new Address::Ipv4Instance(port));
  fd_ = local_address_->socket(Address::SocketType::Stream);
  RELEASE_ASSERT(fd_ != -1);

  int on = 1;
  int rc = setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
  RELEASE_ASSERT(rc != -1);

  if (bind_to_port) {
    doBind();
  }
}

TcpListenSocket::TcpListenSocket(int fd, uint32_t port) {
  fd_ = fd;
  local_address_.reset(new Address::Ipv4Instance(port));
}

UdsListenSocket::UdsListenSocket(const std::string& uds_path) {
  remove(uds_path.c_str());
  local_address_.reset(new Address::PipeInstance(uds_path));
  fd_ = local_address_->socket(Address::SocketType::Stream);
  RELEASE_ASSERT(fd_ != -1);
  doBind();
}

} // Network
