#include "source/extensions/quic/server_preferred_address/server_preferred_address.h"

namespace Envoy {
namespace Quic {

namespace {
quic::QuicSocketAddress ipOrAddressToAddress(const quic::QuicSocketAddress& address, int32_t port) {
  if (address.port() == 0) {
    return quic::QuicSocketAddress(address.host(), port);
  }

  return address;
}
} // namespace

EnvoyQuicServerPreferredAddressConfig::Addresses
ServerPreferredAddressConfig::getServerPreferredAddresses(
    const Network::Address::InstanceConstSharedPtr& local_address) {
  int32_t port = local_address->ip()->port();
  Addresses addresses;
  addresses.ipv4_ = ipOrAddressToAddress(v4_.spa_, port);
  addresses.ipv6_ = ipOrAddressToAddress(v6_.spa_, port);
  addresses.dnat_ipv4_ = quic::QuicSocketAddress(v4_.dnat_, port);
  addresses.dnat_ipv6_ = quic::QuicSocketAddress(v6_.dnat_, port);
  return addresses;
}

} // namespace Quic
} // namespace Envoy
