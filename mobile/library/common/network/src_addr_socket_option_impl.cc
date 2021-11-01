#include "library/common/network/src_addr_socket_option_impl.h"

#include "envoy/config/core/v3/base.pb.h"

#include "source/common/common/assert.h"

namespace Envoy {
namespace Network {

SrcAddrSocketOptionImpl::SrcAddrSocketOptionImpl(
    Network::Address::InstanceConstSharedPtr source_address)
    : source_address_(std::move(source_address)) {
  ASSERT(source_address_->type() == Network::Address::Type::Ip);
}

bool SrcAddrSocketOptionImpl::setOption(
    Network::Socket& socket, envoy::config::core::v3::SocketOption::SocketState state) const {

  if (state == envoy::config::core::v3::SocketOption::STATE_PREBIND) {
    socket.connectionInfoProvider().setLocalAddress(source_address_);
  }

  return true;
}

/**
 * Inserts an address, already in network order, to a byte array.
 */
template <typename T> void addressIntoVector(std::vector<uint8_t>& vec, const T& address) {
  const uint8_t* byte_array = reinterpret_cast<const uint8_t*>(&address);
  vec.insert(vec.end(), byte_array, byte_array + sizeof(T));
}

void SrcAddrSocketOptionImpl::hashKey(std::vector<uint8_t>& key) const {

  // Note: we're assuming that there cannot be a conflict between IPv6 addresses here. If an IPv4
  // address is mapped into an IPv6 address using an IPv4-Mapped IPv6 Address (RFC4921), then it's
  // possible the hashes will be different despite the IP address used by the connection being
  // the same.
  if (source_address_->ip()->version() == Network::Address::IpVersion::v4) {
    // note raw_address is already in network order
    uint32_t raw_address = source_address_->ip()->ipv4()->address();
    addressIntoVector(key, raw_address);
  } else if (source_address_->ip()->version() == Network::Address::IpVersion::v6) {
    // note raw_address is already in network order
    absl::uint128 raw_address = source_address_->ip()->ipv6()->address();
    addressIntoVector(key, raw_address);
  }
}

absl::optional<Network::Socket::Option::Details> SrcAddrSocketOptionImpl::getOptionDetails(
    const Network::Socket&, envoy::config::core::v3::SocketOption::SocketState) const {
  // no details for this option.
  return absl::nullopt;
}

} // namespace Network
} // namespace Envoy
