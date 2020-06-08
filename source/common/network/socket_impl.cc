#include "common/network/socket_impl.h"

#include "envoy/common/exception.h"

#include "common/api/os_sys_calls_impl.h"
#include "common/network/address_impl.h"
#include "common/network/io_socket_handle_impl.h"
#include "common/network/socket_interface_impl.h"

namespace Envoy {
namespace Network {

SocketImpl::SocketImpl(Socket::Type type, Address::Type addr_type, Address::IpVersion version)
    : io_handle_(SocketInterfaceSingleton::get().socket(type, addr_type, version)) {}

SocketImpl::SocketImpl(Socket::Type sock_type, const Address::InstanceConstSharedPtr addr)
    : io_handle_(SocketInterfaceSingleton::get().socket(sock_type, addr)), sock_type_(sock_type),
      addr_type_(addr->type()) {}

SocketImpl::SocketImpl(IoHandlePtr&& io_handle,
                       const Address::InstanceConstSharedPtr& local_address)
    : io_handle_(std::move(io_handle)), local_address_(local_address) {

  // Should not happen but some tests inject -1 fds
  if (SOCKET_INVALID(io_handle_->fd())) {
    addr_type_ = local_address != nullptr ? local_address->type() : Address::Type::Ip;
    return;
  }

  sockaddr_storage addr;
  socklen_t len = sizeof(addr);
  Api::SysCallIntResult result;

  result = Api::OsSysCallsSingleton::get().getsockname(
      io_handle_->fd(), reinterpret_cast<struct sockaddr*>(&addr), &len);

  // This should never happen in practice but too many tests inject fake fds ...
  if (result.rc_ < 0) {
    addr_type_ = local_address != nullptr ? local_address->type() : Address::Type::Ip;
    return;
  }

  addr_type_ = addr.ss_family == AF_UNIX ? Address::Type::Pipe : Address::Type::Ip;
}

Api::SysCallIntResult SocketImpl::bind(Network::Address::InstanceConstSharedPtr address) {
  Api::SysCallIntResult bind_result;

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
    bind_result = Api::OsSysCallsSingleton::get().bind(io_handle_->fd(), address->sockAddr(),
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

  bind_result = Api::OsSysCallsSingleton::get().bind(io_handle_->fd(), address->sockAddr(),
                                                     address->sockAddrLen());
  if (bind_result.rc_ == 0 && address->ip()->port() == 0) {
    local_address_ = SocketInterfaceSingleton::get().addressFromFd(io_handle_->fd());
  }
  return bind_result;
}

Api::SysCallIntResult SocketImpl::listen(int backlog) {
  return Api::OsSysCallsSingleton::get().listen(io_handle_->fd(), backlog);
}

Api::SysCallIntResult SocketImpl::connect(const Network::Address::InstanceConstSharedPtr address) {
  auto result = Api::OsSysCallsSingleton::get().connect(io_handle_->fd(), address->sockAddr(),
                                                        address->sockAddrLen());
  if (address->type() == Address::Type::Ip) {
    local_address_ = SocketInterfaceSingleton::get().addressFromFd(io_handle_->fd());
  }
  return result;
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

Api::SysCallIntResult SocketImpl::setBlockingForTest(bool blocking) {
  return Api::OsSysCallsSingleton::get().setsocketblocking(io_handle_->fd(), blocking);
}

} // namespace Network
} // namespace Envoy