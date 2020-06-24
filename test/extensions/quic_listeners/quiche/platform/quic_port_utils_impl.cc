// NOLINT(namespace-envoy)

// This file is part of the QUICHE platform implementation, and is not to be
// consumed or referenced directly by other Envoy code. It serves purely as a
// porting layer for QUICHE.

#include "test/extensions/quic_listeners/quiche/platform/quic_port_utils_impl.h"

#include "envoy/network/address.h"

#include "common/common/assert.h"
#include "common/network/utility.h"

#include "test/test_common/environment.h"
#include "test/test_common/network_utility.h"

namespace quic {

int QuicPickServerPortForTestsOrDieImpl() {
  std::vector<Envoy::Network::Address::IpVersion> supported_versions =
      Envoy::TestEnvironment::getIpVersionsForTest();
  ASSERT(!supported_versions.empty());
  // Checking availability under corresponding supported version if test
  // supports v4 only or v6 only.
  // If it supports both v4 and v6, checking availability under v6 with IPV6_V6ONLY
  // set to false is sufficient because such socket can be used on v4-mapped
  // v6 address.
  const Envoy::Network::Address::IpVersion ip_version =
      supported_versions.size() == 1 ? supported_versions[0]
                                     : Envoy::Network::Address::IpVersion::v6;
  auto addr_port = Envoy::Network::Utility::parseInternetAddressAndPort(
      fmt::format("{}:{}", Envoy::Network::Test::getAnyAddressUrlString(ip_version), /*port*/ 0),
      /*v6only*/ false);
  ASSERT(addr_port != nullptr);
  addr_port =
      Envoy::Network::Test::findOrCheckFreePort(addr_port, Envoy::Network::Socket::Type::Datagram);
  if (addr_port != nullptr && addr_port->ip() != nullptr) {
    // Find a port.
    return addr_port->ip()->port();
  }
  RELEASE_ASSERT(false, "Failed to pick a port for test.");
}

} // namespace quic
