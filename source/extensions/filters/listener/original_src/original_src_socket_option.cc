#include "extensions/filters/listener/original_src/original_src_socket_option.h"

#include "common/common/assert.h"

namespace Envoy {
namespace Extensions {
namespace ListenerFilters {
namespace OriginalSrc {

constexpr uint8_t OriginalSrcSocketOption::IPV4_KEY;
constexpr uint8_t OriginalSrcSocketOption::IPV6_KEY;

OriginalSrcSocketOption::OriginalSrcSocketOption(
    Network::Address::InstanceConstSharedPtr src_address)
    : src_address_(std::move(src_address)) {
  // Source transparency only works on IP connections.
  ASSERT(src_address_->type() == Network::Address::Type::Ip);
}

bool OriginalSrcSocketOption::setOption(
    Network::Socket& socket, envoy::api::v2::core::SocketOption::SocketState state) const {

  if (state == envoy::api::v2::core::SocketOption::STATE_PREBIND) {
    socket.setLocalAddress(src_address_);
  }

  // TODO(klarose): Add some UT for this and the failure case when we actually add options to this.
  bool result = true;
  for (const auto& option : options_to_apply_) {
    result &= option->setOption(socket, state);
  }

  return result;
}

/**
 * Inserts an address, already in network order, to a byte array.
 */
template <typename T> void addressIntoVector(std::vector<uint8_t>& vec, const T& address) {
  const uint8_t* byte_array = reinterpret_cast<const uint8_t*>(&address);
  vec.insert(vec.end(), byte_array, byte_array + sizeof(T));
}

void OriginalSrcSocketOption::hashKey(std::vector<uint8_t>& key) const {

  // Note: we're assuming that there cannot be a conflict between IPv6 addresses here. If an IPv4
  // address is mapped into an IPv6 address using an IPv4-Mapped IPv6 Address (RFC4921), then it's
  // possible the hashes will be different despite the IP address used by the connection being
  // the same.
  if (src_address_->ip()->version() == Network::Address::IpVersion::v4) {
    // note raw_address is already in network order
    uint32_t raw_address = src_address_->ip()->ipv4()->address();
    addressIntoVector(key, raw_address);
  } else if (src_address_->ip()->version() == Network::Address::IpVersion::v6) {
    // note raw_address is already in network order
    absl::uint128 raw_address = src_address_->ip()->ipv6()->address();
    addressIntoVector(key, raw_address);
  }
}

absl::optional<Network::Socket::Option::Details>
OriginalSrcSocketOption::getOptionDetails(const Network::Socket&,
                                          envoy::api::v2::core::SocketOption::SocketState) const {
  // TODO(klarose): The option details stuff will likely require a bit of a rework when we actually
  // put options in here to support multiple options at once. Sad.
  NOT_IMPLEMENTED_GCOVR_EXCL_LINE
  return absl::nullopt; // nothing right now.
}

} // namespace OriginalSrc
} // namespace ListenerFilters
} // namespace Extensions
} // namespace Envoy
