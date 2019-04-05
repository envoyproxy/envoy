// NOLINT(namespace-envoy)

// This file is part of the QUICHE platform implementation, and is not to be
// consumed or referenced directly by other Envoy code. It serves purely as a
// porting layer for QUICHE.

#include "extensions/quic_listeners/quiche/platform/quic_port_utils_impl.h"

#include "envoy/network/address.h"

#include "common/common/assert.h"
#include "common/network/utility.h"

#include "test/test_common/environment.h"
#include "test/test_common/network_utility.h"

namespace quic {

int QuicPickUnusedPortOrDieImpl() {
  // Only try to get a port for limited times.
  std::vector<Envoy::Network::Address::IpVersion> supported_versions =
      Envoy::TestEnvironment::getIpVersionsForTest();
  for (size_t i = 0; i < 30000; ++i) {
    uint32_t port = 0;
    bool available = true;
    for (auto ip_version : supported_versions) {
      // Check availability of a port for all supported address families.
      auto addr_port = Envoy::Network::Utility::parseInternetAddressAndPort(
          fmt::format("{}:{}", Envoy::Network::Test::getAnyAddressUrlString(ip_version), port),
          /*v6only*/ false);
      ASSERT(addr_port != nullptr);
      addr_port = Envoy::Network::Test::findOrCheckFreePort(
          addr_port, Envoy::Network::Address::SocketType::Datagram);
      if (addr_port == nullptr || addr_port->ip() == nullptr) {
        available = false;
        break;
      }
      if (port == 0) {
        // Just get a port from findOrCheckFreePort(), check its usability in the rest address
        // families.
        port = addr_port->ip()->port();
      } else {
        ASSERT(port == addr_port->ip()->port());
      }
    }
    if (available) {
      return port;
    }
  }
  RELEASE_ASSERT(false, "Failed to pick a port for test.");
}

} // namespace quic
