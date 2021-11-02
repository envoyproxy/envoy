#pragma once

#include "envoy/common/platform.h"
#include "envoy/config/core/v3/base.pb.h"
#include "envoy/network/listen_socket.h"

#include "source/common/common/logger.h"
#include "source/common/network/socket_option_impl.h"

#include "absl/types/optional.h"

namespace Envoy {
namespace Network {

class AddrFamilyAwareSocketOptionImpl : public Socket::Option,
                                        Logger::Loggable<Logger::Id::connection> {
public:
  AddrFamilyAwareSocketOptionImpl(envoy::config::core::v3::SocketOption::SocketState in_state,
                                  SocketOptionName ipv4_optname, SocketOptionName ipv6_optname,
                                  int value)
      : AddrFamilyAwareSocketOptionImpl(in_state, ipv4_optname, value, ipv6_optname, value) {}
  AddrFamilyAwareSocketOptionImpl(envoy::config::core::v3::SocketOption::SocketState in_state,
                                  SocketOptionName ipv4_optname, int ipv4_value,
                                  SocketOptionName ipv6_optname, int ipv6_value)
      : ipv4_option_(std::make_unique<SocketOptionImpl>(in_state, ipv4_optname, ipv4_value)),
        ipv6_option_(std::make_unique<SocketOptionImpl>(in_state, ipv6_optname, ipv6_value)) {}
  AddrFamilyAwareSocketOptionImpl(envoy::config::core::v3::SocketOption::SocketState in_state,
                                  SocketOptionName ipv4_optname, absl::string_view ipv4_value,
                                  SocketOptionName ipv6_optname, absl::string_view ipv6_value)
      : ipv4_option_(std::make_unique<SocketOptionImpl>(in_state, ipv4_optname, ipv4_value)),
        ipv6_option_(std::make_unique<SocketOptionImpl>(in_state, ipv6_optname, ipv6_value)) {}
  AddrFamilyAwareSocketOptionImpl(Socket::OptionConstPtr&& ipv4_option,
                                  Socket::OptionConstPtr&& ipv6_option)
      : ipv4_option_(std::move(ipv4_option)), ipv6_option_(std::move(ipv6_option)) {}

  // Socket::Option
  bool setOption(Socket& socket,
                 envoy::config::core::v3::SocketOption::SocketState state) const override;
  void hashKey(std::vector<uint8_t>& hash_key) const override {
    // Add both sub-options to the hash.
    ipv4_option_->hashKey(hash_key);
    ipv6_option_->hashKey(hash_key);
  }
  absl::optional<Details>
  getOptionDetails(const Socket& socket,
                   envoy::config::core::v3::SocketOption::SocketState state) const override;
  bool isSupported() const override { return true; }

  /**
   * Set a socket option that applies at both IPv4 and IPv6 socket levels. When the underlying FD
   * is IPv6, this function will attempt to set at IPv6 unless the platform only supports the
   * option at the IPv4 level.
   * @param socket.
   * @param ipv4_optname SocketOptionName for IPv4 level. Set to empty if not supported on
   * platform.
   * @param ipv6_optname SocketOptionName for IPv6 level. Set to empty if not supported on
   * platform.
   * @param optval as per setsockopt(2).
   * @param optlen as per setsockopt(2).
   * @return int as per setsockopt(2). `ENOTSUP` is returned if the option is not supported on the
   * platform for fd after the above option level fallback semantics are taken into account or the
   *         socket is non-IP.
   */
  static bool setIpSocketOption(Socket& socket,
                                envoy::config::core::v3::SocketOption::SocketState state,
                                const Socket::Option& ipv4_option,
                                const Socket::Option& ipv6_option);

private:
  const Socket::OptionConstPtr ipv4_option_;
  const Socket::OptionConstPtr ipv6_option_;
};

} // namespace Network
} // namespace Envoy
