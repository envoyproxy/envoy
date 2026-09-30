#include "source/common/upstream/udp_source_address_policy.h"

#include <utility>

#include "source/common/common/assert.h"
#include "source/common/network/socket_option_factory.h"

namespace Envoy {
namespace Upstream {

UdpSourceAddressPolicy::UdpSourceAddressPolicy(
    Mode mode, UpstreamLocalAddress upstream_local_address,
    Network::Address::InstanceConstSharedPtr packet_source_address)
    : mode_(mode), upstream_local_address_(std::move(upstream_local_address)),
      packet_source_address_(std::move(packet_source_address)) {}

UdpSourceAddressPolicy UdpSourceAddressPolicy::kernelSelected() {
  return UdpSourceAddressPolicy(Mode::KernelSelected, {nullptr, nullptr}, nullptr);
}

UdpSourceAddressPolicy
UdpSourceAddressPolicy::fromUpstreamLocalAddress(UpstreamLocalAddress upstream_local_address) {
  const Mode mode =
      upstream_local_address.address_ == nullptr ? Mode::KernelSelected : Mode::Configured;
  return UdpSourceAddressPolicy(mode, std::move(upstream_local_address), nullptr);
}

UdpSourceAddressPolicy
UdpSourceAddressPolicy::transparent(Network::Address::InstanceConstSharedPtr source_address) {
  ASSERT(source_address != nullptr);
  ASSERT(source_address->ip() != nullptr);
  Network::Socket::OptionsSharedPtr socket_options =
      Network::SocketOptionFactory::buildIpTransparentOptions();
  return UdpSourceAddressPolicy(Mode::Transparent, {nullptr, std::move(socket_options)},
                                std::move(source_address));
}

const Network::Address::Ip* UdpSourceAddressPolicy::packetSourceIp() const {
  return packet_source_address_ != nullptr ? packet_source_address_->ip() : nullptr;
}

UdpSourceAddressPolicy::SocketSetupResult
UdpSourceAddressPolicy::prepareSocket(Network::Socket& socket) const {
  if (upstream_local_address_.socket_options_ != nullptr &&
      !Network::Socket::applyOptions(upstream_local_address_.socket_options_, socket,
                                     envoy::config::core::v3::SocketOption::STATE_PREBIND)) {
    return {SocketSetupResult::FailureReason::SocketOption, 0};
  }

  if (upstream_local_address_.address_ != nullptr) {
    const Api::SysCallIntResult result = socket.bind(upstream_local_address_.address_);
    if (result.return_value_ < 0) {
      return {SocketSetupResult::FailureReason::Bind, result.errno_};
    }
  }

  return {};
}

} // namespace Upstream
} // namespace Envoy
