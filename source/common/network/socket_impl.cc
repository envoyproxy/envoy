#include "common/network/socket_impl.h"

#include "envoy/common/exception.h"

#include "common/api/os_sys_calls_impl.h"
#include "common/network/address_impl.h"
#include "common/network/io_socket_handle_impl.h"

namespace Envoy {
namespace Network {

IoHandlePtr SocketInterface::socket(Address::SocketType socket_type, Address::Type addr_type,
                                    Address::IpVersion version) {
#if defined(__APPLE__) || defined(WIN32)
  int flags = 0;
#else
  int flags = SOCK_NONBLOCK;
#endif

  if (socket_type == Address::SocketType::Stream) {
    flags |= SOCK_STREAM;
  } else {
    flags |= SOCK_DGRAM;
  }

  int domain;
  if (addr_type == Address::Type::Ip) {
    if (version == Address::IpVersion::v6) {
      domain = AF_INET6;
    } else {
      ASSERT(version == Address::IpVersion::v4);
      domain = AF_INET;
    }
  } else {
    ASSERT(addr_type == Address::Type::Pipe);
    domain = AF_UNIX;
  }

  const Api::SysCallSocketResult result = Api::OsSysCallsSingleton::get().socket(domain, flags, 0);
  RELEASE_ASSERT(SOCKET_VALID(result.rc_),
                 fmt::format("socket(2) failed, got error: {}", strerror(result.errno_)));
  IoHandlePtr io_handle = std::make_unique<IoSocketHandleImpl>(result.rc_);

#if defined(__APPLE__) || defined(WIN32)
  // Cannot set SOCK_NONBLOCK as a ::socket flag.
  const int rc = Api::OsSysCallsSingleton::get().setsocketblocking(io_handle->fd(), false).rc_;
  RELEASE_ASSERT(!SOCKET_FAILURE(rc), "");
#endif

  return io_handle;
}

IoHandlePtr SocketInterface::socket(Address::SocketType socket_type,
                                    const Address::InstanceConstSharedPtr addr) {
  Address::IpVersion ip_version = addr->ip() ? addr->ip()->version() : Address::IpVersion::v4;
  IoHandlePtr io_handle = SocketInterface::socket(socket_type, addr->type(), ip_version);
  if (addr->type() == Address::Type::Ip && addr->ip()->version() == Address::IpVersion::v6) {
    // Setting IPV6_V6ONLY restricts the IPv6 socket to IPv6 connections only.
    const int v6only = addr->ip()->ipv6()->v6only();
    const Api::SysCallIntResult result = Api::OsSysCallsSingleton::get().setsockopt(
        io_handle->fd(), IPPROTO_IPV6, IPV6_V6ONLY, reinterpret_cast<const char*>(&v6only),
        sizeof(v6only));
    RELEASE_ASSERT(!SOCKET_FAILURE(result.rc_), "");
  }
  return io_handle;
}

bool SocketInterface::ipFamilySupported(int domain) {
  Api::OsSysCalls& os_sys_calls = Api::OsSysCallsSingleton::get();
  const Api::SysCallSocketResult result = os_sys_calls.socket(domain, SOCK_STREAM, 0);
  if (SOCKET_VALID(result.rc_)) {
    RELEASE_ASSERT(os_sys_calls.close(result.rc_).rc_ == 0,
                   fmt::format("Fail to close fd: response code {}", strerror(result.rc_)));
  }
  return SOCKET_VALID(result.rc_);
}

Address::InstanceConstSharedPtr SocketInterface::addressFromFd(os_fd_t fd) {
  sockaddr_storage ss;
  socklen_t ss_len = sizeof ss;
  auto& os_sys_calls = Api::OsSysCallsSingleton::get();
  Api::SysCallIntResult result =
      os_sys_calls.getsockname(fd, reinterpret_cast<sockaddr*>(&ss), &ss_len);
  if (result.rc_ != 0) {
    throw EnvoyException(fmt::format("getsockname failed for '{}': ({}) {}", fd, result.errno_,
                                     strerror(result.errno_)));
  }
  int socket_v6only = 0;
  if (ss.ss_family == AF_INET6) {
    socklen_t size_int = sizeof(socket_v6only);
    result = os_sys_calls.getsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, &socket_v6only, &size_int);
#ifdef WIN32
    // On Windows, it is possible for this getsockopt() call to fail.
    // This can happen if the address we are trying to connect to has nothing
    // listening. So we can't use RELEASE_ASSERT and instead must throw an
    // exception
    if (SOCKET_FAILURE(result.rc_)) {
      throw EnvoyException(fmt::format("getsockopt failed for '{}': ({}) {}", fd, result.errno_,
                                       strerror(result.errno_)));
    }
#else
    RELEASE_ASSERT(result.rc_ == 0, "");
#endif
  }
  return Address::addressFromSockAddr(ss, ss_len, socket_v6only);
}

Address::InstanceConstSharedPtr SocketInterface::peerAddressFromFd(os_fd_t fd) {
  sockaddr_storage ss;
  socklen_t ss_len = sizeof ss;
  auto& os_sys_calls = Api::OsSysCallsSingleton::get();
  Api::SysCallIntResult result =
      os_sys_calls.getpeername(fd, reinterpret_cast<sockaddr*>(&ss), &ss_len);
  if (result.rc_ != 0) {
    throw EnvoyException(
        fmt::format("getpeername failed for '{}': {}", fd, strerror(result.errno_)));
  }
#ifdef __APPLE__
  if (ss_len == sizeof(sockaddr) && ss.ss_family == AF_UNIX)
#else
  if (ss_len == sizeof(sa_family_t) && ss.ss_family == AF_UNIX)
#endif
  {
    // For Unix domain sockets, can't find out the peer name, but it should match our own
    // name for the socket (i.e. the path should match, barring any namespace or other
    // mechanisms to hide things, of which there are many).
    ss_len = sizeof ss;
    result = os_sys_calls.getsockname(fd, reinterpret_cast<sockaddr*>(&ss), &ss_len);
    if (result.rc_ != 0) {
      throw EnvoyException(
          fmt::format("getsockname failed for '{}': {}", fd, strerror(result.errno_)));
    }
  }
  return Address::addressFromSockAddr(ss, ss_len);
}

Api::SysCallIntResult SocketInterface::getHostName(char* name, size_t length) {
  return Api::OsSysCallsSingleton::get().gethostname(name, length);
}

SocketImpl::SocketImpl(Address::SocketType type, Address::Type addr_type,
                       Address::IpVersion version)
    : io_handle_(SocketInterface::socket(type, addr_type, version)), sock_type_(type),
      addr_type_(addr_type) {}

SocketImpl::SocketImpl(Address::SocketType sock_type, const Address::InstanceConstSharedPtr addr)
    : io_handle_(SocketInterface::socket(sock_type, addr)), sock_type_(sock_type),
      addr_type_(addr->type()) {}

SocketImpl::SocketImpl(IoHandlePtr&& io_handle,
                       const Address::InstanceConstSharedPtr& local_address)
    : io_handle_(std::move(io_handle)), local_address_(local_address) {

  // Should not happen but some tests inject -1 fds
  if (SOCKET_INVALID(io_handle_->fd())) {
    return;
  }

  sockaddr_storage addr;
  socklen_t len = sizeof(addr);
  Api::SysCallIntResult result;

  result = Api::OsSysCallsSingleton::get().getsockname(
      io_handle_->fd(), reinterpret_cast<struct sockaddr*>(&addr), &len);

  // This should never happen in practice but too many tests inject fake fds ...
  if (result.rc_ < 0) {
    return;
  }

  if (addr.ss_family == AF_UNIX) {
    addr_type_ = Address::Type::Pipe;
  } else {
    addr_type_ = Address::Type::Ip;
  }
}

Api::SysCallIntResult SocketImpl::bind(Network::Address::InstanceConstSharedPtr address) {
  if (address->type() == Address::Type::Pipe) {
    const Address::Pipe* pipe = address->pipe();
    const auto* pipe_sa = reinterpret_cast<const sockaddr_un*>(address->sockAddr());
    bool abstract_namespace = address->pipe()->abstractNamespace();
    if (!abstract_namespace) {
      // Try to unlink an existing filesystem object at the requested path. Ignore
      // errors -- it's fine if the path doesn't exist, and if it exists but can't
      // be unlinked then `::bind()` will generate a reasonable errno.
      unlink(pipe_sa->sun_path);
    }
    // Not storing a reference to syscalls singleton because of unit test mocks
    auto bind_result = Api::OsSysCallsSingleton::get().bind(io_handle_->fd(), address->sockAddr(),
                                                            address->sockAddrLen());
    if (pipe->mode() != 0 && !abstract_namespace && bind_result.rc_ == 0) {
      auto set_permissions = Api::OsSysCallsSingleton::get().chmod(pipe_sa->sun_path, pipe->mode());
      if (set_permissions.rc_ != 0) {
        throw EnvoyException(fmt::format("Failed to create socket with mode {}: {}",
                                         std::to_string(pipe->mode()),
                                         strerror(set_permissions.errno_)));
      }
    }
    return bind_result;
  }

  return Api::OsSysCallsSingleton::get().bind(io_handle_->fd(), address->sockAddr(),
                                              address->sockAddrLen());
}

Api::SysCallIntResult SocketImpl::listen(int backlog) {
  return Api::OsSysCallsSingleton::get().listen(io_handle_->fd(), backlog);
}

Api::SysCallIntResult SocketImpl::connect(const Network::Address::InstanceConstSharedPtr address) {
  return Api::OsSysCallsSingleton::get().connect(io_handle_->fd(), address->sockAddr(),
                                                 address->sockAddrLen());
}

Api::SysCallIntResult SocketImpl::setSocketOption(int level, int optname, const void* optval,
                                                  socklen_t optlen) {
  return Api::OsSysCallsSingleton::get().setsockopt(io_handle_->fd(), level, optname, optval,
                                                    optlen);
}

Api::SysCallIntResult SocketImpl::getSocketOption(int level, int optname, void* optval,
                                                  socklen_t* optlen) {
  return Api::OsSysCallsSingleton::get().getsockopt(io_handle_->fd(), level, optname, optval,
                                                    optlen);
}

Api::SysCallIntResult SocketImpl::setBlocking(bool blocking) {
  return Api::OsSysCallsSingleton::get().setsocketblocking(io_handle_->fd(), blocking);
}

} // namespace Network
} // namespace Envoy