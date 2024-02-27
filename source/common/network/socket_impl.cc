#include "source/common/network/socket_impl.h"

#include "envoy/common/exception.h"
#include "envoy/network/socket_interface.h"

#include "source/common/api/os_sys_calls_impl.h"
#include "source/common/common/utility.h"

namespace Envoy {
namespace Network {

SocketImpl::SocketImpl(Socket::Type sock_type,
                       const Address::InstanceConstSharedPtr& address_for_io_handle,
                       const Address::InstanceConstSharedPtr& remote_address,
                       const SocketCreationOptions& options)
    : io_handle_(ioHandleForAddr(sock_type, address_for_io_handle, options)),
      connection_info_provider_(
          std::make_shared<ConnectionInfoSetterImpl>(nullptr, remote_address)),
      sock_type_(sock_type), addr_type_(address_for_io_handle->type()) {}

SocketImpl::SocketImpl(IoHandlePtr&& io_handle,
                       const Address::InstanceConstSharedPtr& local_address,
                       const Address::InstanceConstSharedPtr& remote_address)
    : io_handle_(std::move(io_handle)),
      connection_info_provider_(
          std::make_shared<ConnectionInfoSetterImpl>(local_address, remote_address)),
      sock_type_(Network::Socket::Type::Stream) {

  if (connection_info_provider_->localAddress() != nullptr) {
    addr_type_ = connection_info_provider_->localAddress()->type();
    return;
  }

  if (connection_info_provider_->remoteAddress() != nullptr) {
    addr_type_ = connection_info_provider_->remoteAddress()->type();
    return;
  }

  if (!io_handle_) {
    // This can happen iff system socket creation fails.
    ENVOY_LOG_MISC(warn, "Created socket with null io handle");
    return;
  }

  // Should not happen but some tests inject -1 fds
  if (!io_handle_->isOpen()) {
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
    if (pipe->mode() != 0 && !abstract_namespace && bind_result.return_value_ == 0) {
      auto set_permissions = Api::OsSysCallsSingleton::get().chmod(pipe_sa->sun_path, pipe->mode());
      if (set_permissions.return_value_ != 0) {
        return set_permissions;
      }
    }
    return bind_result;
  }

  bind_result = io_handle_->bind(address);
  if (bind_result.return_value_ == 0 && address->ip()->port() == 0) {
    connection_info_provider_->setLocalAddress(io_handle_->localAddress());
  }
  return bind_result;
}

Api::SysCallIntResult SocketImpl::listen(int backlog) { return io_handle_->listen(backlog); }

Api::SysCallIntResult SocketImpl::connect(const Network::Address::InstanceConstSharedPtr address) {
  auto result = io_handle_->connect(address);
  if (address->type() == Address::Type::Ip) {
    connection_info_provider_->setLocalAddress(io_handle_->localAddress());
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

Api::SysCallIntResult SocketImpl::ioctl(unsigned long control_code, void* in_buffer,
                                        unsigned long in_buffer_len, void* out_buffer,
                                        unsigned long out_buffer_len,
                                        unsigned long* bytes_returned) {
  return io_handle_->ioctl(control_code, in_buffer, in_buffer_len, out_buffer, out_buffer_len,
                           bytes_returned);
}

Api::SysCallIntResult SocketImpl::setBlockingForTest(bool blocking) {
  return io_handle_->setBlocking(blocking);
}

absl::optional<Address::IpVersion> SocketImpl::ipVersion() const {
  if (addr_type_ == Address::Type::Ip) {
    // Always hit after socket is initialized, i.e., accepted or connected
    if (connection_info_provider_->localAddress() != nullptr) {
      return connection_info_provider_->localAddress()->ip()->version();
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
