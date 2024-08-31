#include "source/common/network/io_socket_handle_base_impl.h"

#include "envoy/buffer/buffer.h"
#include "envoy/common/exception.h"
#include "envoy/event/dispatcher.h"

#include "source/common/api/os_sys_calls_impl.h"
#include "source/common/common/assert.h"
#include "source/common/common/utility.h"
#include "source/common/network/address_impl.h"
#include "source/common/network/io_socket_error_impl.h"
#include "source/common/network/io_socket_handle_impl.h"
#include "source/common/network/socket_interface_impl.h"

namespace Envoy {
namespace Network {

IoSocketHandleBaseImpl::IoSocketHandleBaseImpl(os_fd_t fd, bool socket_v6only,
                                               absl::optional<int> domain)
    : fd_(fd), socket_v6only_(socket_v6only), domain_(domain) {}

IoSocketHandleBaseImpl::~IoSocketHandleBaseImpl() {
  if (SOCKET_VALID(fd_)) {
    // The TLS slot has been shut down by this moment with IoUring wiped out, thus
    // better use this posix system call instead of IoSocketHandleBaseImpl::close().
    ::close(fd_);
  }
}

bool IoSocketHandleBaseImpl::isOpen() const { return SOCKET_VALID(fd_); }

bool IoSocketHandleBaseImpl::wasConnected() const { return was_connected_; }

bool IoSocketHandleBaseImpl::supportsMmsg() const {
  return Api::OsSysCallsSingleton::get().supportsMmsg();
}

bool IoSocketHandleBaseImpl::supportsUdpGro() const {
  return Api::OsSysCallsSingleton::get().supportsUdpGro();
}

Api::SysCallIntResult IoSocketHandleBaseImpl::setOption(int level, int optname, const void* optval,
                                                        socklen_t optlen) {
  return Api::OsSysCallsSingleton::get().setsockopt(fd_, level, optname, optval, optlen);
}

Api::SysCallIntResult IoSocketHandleBaseImpl::getOption(int level, int optname, void* optval,
                                                        socklen_t* optlen) {
  return Api::OsSysCallsSingleton::get().getsockopt(fd_, level, optname, optval, optlen);
}

Api::SysCallIntResult IoSocketHandleBaseImpl::ioctl(unsigned long control_code, void* in_buffer,
                                                    unsigned long in_buffer_len, void* out_buffer,
                                                    unsigned long out_buffer_len,
                                                    unsigned long* bytes_returned) {
  return Api::OsSysCallsSingleton::get().ioctl(fd_, control_code, in_buffer, in_buffer_len,
                                               out_buffer, out_buffer_len, bytes_returned);
}

Api::SysCallIntResult IoSocketHandleBaseImpl::setBlocking(bool blocking) {
  return Api::OsSysCallsSingleton::get().setsocketblocking(fd_, blocking);
}

absl::optional<int> IoSocketHandleBaseImpl::domain() { return domain_; }

Address::InstanceConstSharedPtr IoSocketHandleBaseImpl::localAddress() {
  sockaddr_storage ss;
  socklen_t ss_len = sizeof(ss);
  memset(&ss, 0, ss_len);
  auto& os_sys_calls = Api::OsSysCallsSingleton::get();
  Api::SysCallIntResult result =
      os_sys_calls.getsockname(fd_, reinterpret_cast<sockaddr*>(&ss), &ss_len);
  if (result.return_value_ != 0) {
    throwEnvoyExceptionOrPanic(fmt::format("getsockname failed for '{}': ({}) {}", fd_,
                                           result.errno_, errorDetails(result.errno_)));
  }
  return Address::addressFromSockAddrOrThrow(ss, ss_len, socket_v6only_);
}

Address::InstanceConstSharedPtr IoSocketHandleBaseImpl::peerAddress() {
  sockaddr_storage ss;
  socklen_t ss_len = sizeof(ss);
  memset(&ss, 0, ss_len);
  auto& os_sys_calls = Api::OsSysCallsSingleton::get();
  Api::SysCallIntResult result =
      os_sys_calls.getpeername(fd_, reinterpret_cast<sockaddr*>(&ss), &ss_len);
  if (result.return_value_ != 0) {
    throwEnvoyExceptionOrPanic(
        fmt::format("getpeername failed for '{}': {}", fd_, errorDetails(result.errno_)));
  }

  if (static_cast<unsigned int>(ss_len) >=
          (offsetof(sockaddr_storage, ss_family) + sizeof(ss.ss_family)) &&
      ss.ss_family == AF_UNIX) {
    // For Unix domain sockets, can't find out the peer name, but it should match our own
    // name for the socket (i.e. the path should match, barring any namespace or other
    // mechanisms to hide things, of which there are many).
    ss_len = sizeof(ss);
    result = os_sys_calls.getsockname(fd_, reinterpret_cast<sockaddr*>(&ss), &ss_len);
    if (result.return_value_ != 0) {
      throwEnvoyExceptionOrPanic(
          fmt::format("getsockname failed for '{}': {}", fd_, errorDetails(result.errno_)));
    }
  }
  return Address::addressFromSockAddrOrThrow(ss, ss_len, socket_v6only_);
}

absl::optional<std::chrono::milliseconds> IoSocketHandleBaseImpl::lastRoundTripTime() {
  Api::EnvoyTcpInfo info;
  auto result = Api::OsSysCallsSingleton::get().socketTcpInfo(fd_, &info);
  if (!result.return_value_) {
    return {};
  }
  return std::chrono::duration_cast<std::chrono::milliseconds>(info.tcpi_rtt);
}

absl::optional<uint64_t> IoSocketHandleBaseImpl::congestionWindowInBytes() const {
  Api::EnvoyTcpInfo info;
  auto result = Api::OsSysCallsSingleton::get().socketTcpInfo(fd_, &info);
  if (!result.return_value_) {
    return {};
  }
  return info.tcpi_snd_cwnd;
}

absl::optional<std::string> IoSocketHandleBaseImpl::interfaceName() {
  auto& os_syscalls_singleton = Api::OsSysCallsSingleton::get();
  if (!os_syscalls_singleton.supportsGetifaddrs()) {
    return absl::nullopt;
  }

  Address::InstanceConstSharedPtr socket_address = localAddress();
  if (!socket_address || socket_address->type() != Address::Type::Ip) {
    return absl::nullopt;
  }

  Api::InterfaceAddressVector interface_addresses{};
  const Api::SysCallIntResult rc = os_syscalls_singleton.getifaddrs(interface_addresses);
  if (rc.return_value_ != 0) {
    ENVOY_LOG_EVERY_POW_2(warn, "getifaddrs error: {}", rc.errno_);
    return absl::nullopt;
  }

  absl::optional<std::string> selected_interface_name{};
  for (const auto& interface_address : interface_addresses) {
    if (!interface_address.interface_addr_) {
      continue;
    }

    if (socket_address->ip()->version() == interface_address.interface_addr_->ip()->version()) {
      // Compare address _without port_.
      // TODO: create common addressAsStringWithoutPort method to simplify code here.
      absl::uint128 socket_address_value;
      absl::uint128 interface_address_value;
      switch (socket_address->ip()->version()) {
      case Address::IpVersion::v4:
        socket_address_value = socket_address->ip()->ipv4()->address();
        interface_address_value = interface_address.interface_addr_->ip()->ipv4()->address();
        break;
      case Address::IpVersion::v6:
        socket_address_value = socket_address->ip()->ipv6()->address();
        interface_address_value = interface_address.interface_addr_->ip()->ipv6()->address();
        break;
      default:
        ENVOY_BUG(false, fmt::format("unexpected IP family {}",
                                     static_cast<int>(socket_address->ip()->version())));
      }

      if (socket_address_value == interface_address_value) {
        selected_interface_name = interface_address.interface_name_;
        break;
      }
    }
  }

  return selected_interface_name;
}

} // namespace Network
} // namespace Envoy
