#include "source/common/network/addr_family_aware_socket_option_impl.h"

#include "envoy/common/exception.h"
#include "envoy/common/optref.h"
#include "envoy/config/core/v3/base.pb.h"

#include "source/common/api/os_sys_calls_impl.h"
#include "source/common/common/assert.h"
#include "source/common/network/address_impl.h"
#include "source/common/network/socket_option_impl.h"

namespace Envoy {
namespace Network {

namespace {

OptRef<const Socket::Option> getOptionForSocket(const Socket& socket,
                                                const Socket::Option& ipv4_option,
                                                const Socket::Option& ipv6_option) {
  auto version = socket.ipVersion();
  if (!version.has_value()) {
    return {};
  }

  // If the FD is v4, we can only try the IPv4 variant.
  if (*version == Network::Address::IpVersion::v4) {
    return {ipv4_option};
  }
  // If the FD is v6, we first try the IPv6 variant if the platform supports it and fallback to the
  // IPv4 variant otherwise.
  ASSERT(*version == Network::Address::IpVersion::v6);
  if (ipv6_option.isSupported()) {
    return {ipv6_option};
  }
  return {ipv4_option};
}

} // namespace

bool AddrFamilyAwareSocketOptionImpl::setOption(
    Socket& socket, envoy::config::core::v3::SocketOption::SocketState state) const {
  return setIpSocketOption(socket, state, *ipv4_option_, *ipv6_option_);
}

absl::optional<Socket::Option::Details> AddrFamilyAwareSocketOptionImpl::getOptionDetails(
    const Socket& socket, envoy::config::core::v3::SocketOption::SocketState state) const {
  auto option = getOptionForSocket(socket, *ipv4_option_, *ipv6_option_);

  if (!option.has_value()) {
    return absl::nullopt;
  }

  return option.value().get().getOptionDetails(socket, state);
}

bool AddrFamilyAwareSocketOptionImpl::setIpSocketOption(
    Socket& socket, envoy::config::core::v3::SocketOption::SocketState state,
    const Socket::Option& ipv4_option, const Socket::Option& ipv6_option) {
  auto option = getOptionForSocket(socket, ipv4_option, ipv6_option);

  if (!option.has_value()) {
    ENVOY_LOG(warn, "Failed to set IP socket option on non-IP socket");
    return false;
  }

  return option.value().get().setOption(socket, state);
}

} // namespace Network
} // namespace Envoy
