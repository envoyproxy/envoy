#include "common/network/socket_impl.h"

#include "envoy/common/exception.h"

#include "common/api/os_sys_calls_impl.h"
#include "common/common/utility.h"
#include "common/network/address_impl.h"
#include "common/network/io_socket_handle_impl.h"
#include "common/network/socket_interface_impl.h"

namespace Envoy {
namespace Network {

SocketImpl::SocketImpl(Socket::Type sock_type, const Address::InstanceConstSharedPtr addr)
    : io_handle_(SocketInterfaceSingleton::get().socket(sock_type, addr)), sock_type_(sock_type),
      addr_type_(addr->type()) {}

SocketImpl::SocketImpl(IoHandlePtr&& io_handle,
                       const Address::InstanceConstSharedPtr& local_address)
    : io_handle_(std::move(io_handle)), local_address_(local_address) {

  if (local_address_ != nullptr) {
    addr_type_ = local_address_->type();
    return;
  }

  // Should not happen but some tests inject -1 fds
  if (SOCKET_INVALID(io_handle_->fd())) {
    return;
  }

  auto domain = io_handle_->domain();

  // This should never happen in practice but too many tests inject fake fds ...
  if (!domain.has_value()) {
    return;
  }

  addr_type_ = *domain == AF_UNIX ? Address::Type::Pipe : Address::Type::Ip;
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
    bind_result = io_handle_->bind(address);
    if (pipe->mode() != 0 && !abstract_namespace && bind_result.rc_ == 0) {
      auto set_permissions = Api::OsSysCallsSingleton::get().chmod(pipe_sa->sun_path, pipe->mode());
      if (set_permissions.rc_ != 0) {
        throw EnvoyException(fmt::format("Failed to create socket with mode {}: {}",
                                         std::to_string(pipe->mode()),
                                         errorDetails(set_permissions.errno_)));
      }
    }
    return bind_result;
  }

  bind_result = io_handle_->bind(address);
  if (bind_result.rc_ == 0 && address->ip()->port() == 0) {
    local_address_ = io_handle_->localAddress();
  }
  return bind_result;
}

Api::SysCallIntResult SocketImpl::listen(int backlog) { return io_handle_->listen(backlog); }

Api::SysCallIntResult SocketImpl::connect(const Network::Address::InstanceConstSharedPtr address) {
  auto result = io_handle_->connect(address);
  if (address->type() == Address::Type::Ip) {
    local_address_ = io_handle_->localAddress();
  }
  return result;
}

Api::SysCallIntResult SocketImpl::setSocketOption(int level, int optname, const void* optval,
                                                  socklen_t optlen) {
  return io_handle_->setOption(level, optname, optval, optlen);
}

Api::SysCallIntResult SocketImpl::getSocketOption(int level, int optname, void* optval,
                                                  socklen_t* optlen) const {
  return io_handle_->getOption(level, optname, optval, optlen);
}

Api::SysCallIntResult SocketImpl::setBlockingForTest(bool blocking) {
  return io_handle_->setBlocking(blocking);
}

absl::optional<Address::IpVersion> SocketImpl::ipVersion() const {
  if (addr_type_ == Address::Type::Ip) {
    // Always hit after socket is initialized, i.e., accepted or connected
    if (local_address_ != nullptr) {
      return local_address_->ip()->version();
    } else {
      auto domain = io_handle_->domain();
      if (!domain.has_value()) {
        return absl::nullopt;
      }
      if (*domain == AF_INET) {
        return Address::IpVersion::v4;
      } else if (*domain == AF_INET6) {
        return Address::IpVersion::v6;
      } else {
        return absl::nullopt;
      }
    }
  }
  return absl::nullopt;
}

} // namespace Network
} // namespace Envoy
