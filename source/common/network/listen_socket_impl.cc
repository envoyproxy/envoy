#include "listen_socket_impl.h"
#include "utility.h"

#include "envoy/common/exception.h"

#include "common/common/assert.h"

namespace Network {

TcpListenSocket::TcpListenSocket(uint32_t port) : port_(port) {
  AddrInfoPtr address = Utility::resolveTCP("", port);
  fd_ = socket(address->ai_addr->sa_family, SOCK_STREAM | SOCK_NONBLOCK, 0);
  RELEASE_ASSERT(fd_ != -1);

  int on = 1;
  int rc = setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
  RELEASE_ASSERT(rc != -1);

  rc = bind(fd_, address->ai_addr, address->ai_addrlen);
  if (rc == -1) {
    close();
    throw EnvoyException(fmt::format("cannot bind on port {}: {}", port, strerror(errno)));
  }
}

UdsListenSocket::UdsListenSocket(const std::string& uds_path) {
  remove(uds_path.c_str());
  sockaddr_un address = Utility::resolveUnixDomainSocket(uds_path);
  fd_ = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0);
  RELEASE_ASSERT(fd_ != -1);

  int rc = bind(fd_, reinterpret_cast<sockaddr*>(&address), sizeof(sockaddr_un));
  if (rc == -1) {
    close();
    throw EnvoyException(
        fmt::format("cannot bind unix domain socket path {}: {}", uds_path, strerror(errno)));
  }
}

} // Network
