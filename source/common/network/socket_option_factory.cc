#include "common/network/socket_option_factory.h"

#include "common/network/addr_family_aware_socket_impl.h"
#include "common/network/socket_option_impl.h"

namespace Envoy {
namespace Network {

std::unique_ptr<Socket::Option>
SocketOptionFactory::buildTcpKeepaliveOptions(Network::TcpKeepaliveConfig keepalive_config) {
  std::unique_ptr<Network::SocketOptionsImpl> options =
      absl::make_unique<Network::SocketOptionsImpl>();
  options->addOption(absl::make_unique<Network::SocketOptionImpl>(
      Network::Socket::SocketState::PreBind, ENVOY_SOCKET_SO_KEEPALIVE, 1));

  if (keepalive_config.keepalive_probes_.has_value()) {
    options->addOption(absl::make_unique<Network::SocketOptionImpl>(
        Network::Socket::SocketState::PreBind, ENVOY_SOCKET_TCP_KEEPCNT,
        keepalive_config.keepalive_probes_.value()));
  }
  if (keepalive_config.keepalive_interval_.has_value()) {
    options->addOption(absl::make_unique<Network::SocketOptionImpl>(
        Network::Socket::SocketState::PreBind, ENVOY_SOCKET_TCP_KEEPINTVL,
        keepalive_config.keepalive_interval_.value()));
  }
  if (keepalive_config.keepalive_time_.has_value()) {
    options->addOption(absl::make_unique<Network::SocketOptionImpl>(
        Network::Socket::SocketState::PreBind, ENVOY_SOCKET_TCP_KEEPIDLE,
        keepalive_config.keepalive_time_.value()));
  }
  return options;
}

std::unique_ptr<Socket::Option> SocketOptionFactory::buildIpFreebindOptions() {
  return absl::make_unique<Network::AddrFamilyAwareSocketImpl>(
      Network::Socket::SocketState::PreBind, ENVOY_SOCKET_IP_FREEBIND, ENVOY_SOCKET_IPV6_FREEBIND,
      1);
}

std::unique_ptr<Socket::Option> SocketOptionFactory::buildIpTransparentOptions() {
  std::unique_ptr<Network::SocketOptionsImpl> options =
      absl::make_unique<Network::SocketOptionsImpl>();
  options->addOption(absl::make_unique<Network::AddrFamilyAwareSocketImpl>(
      Network::Socket::SocketState::PreBind, ENVOY_SOCKET_IP_TRANSPARENT,
      ENVOY_SOCKET_IPV6_TRANSPARENT, 1));
  options->addOption(absl::make_unique<Network::AddrFamilyAwareSocketImpl>(
      Network::Socket::SocketState::PostBind, ENVOY_SOCKET_IP_TRANSPARENT,
      ENVOY_SOCKET_IPV6_TRANSPARENT, 1));
  return options;
}

std::unique_ptr<Socket::Option>
SocketOptionFactory::buildTcpFastOpenOptions(uint32_t queue_length) {
  return absl::make_unique<Network::SocketOptionImpl>(Network::Socket::SocketState::Listening,
                                                      ENVOY_SOCKET_TCP_FASTOPEN, queue_length);
}

} // namespace Network
} // namespace Envoy