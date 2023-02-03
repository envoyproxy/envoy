#pragma once

#include "envoy/config/typed_config.h"
#include "envoy/network/address.h"

#include "quiche/quic/platform/api/quic_socket_address.h"

namespace Envoy {
namespace Quic {

class EnvoyQuicServerPreferredAddressConfig {
public:
  virtual ~EnvoyQuicServerPreferredAddressConfig() = default;

  /**
   * @param local_address the configured default listening address.
   * Returns a pair of the server preferred addresses in form of {IPv4, IPv6}.
   */
  virtual std::pair<quic::QuicSocketAddress, quic::QuicSocketAddress>
  getServerPreferredAddresses(const Network::Address::InstanceConstSharedPtr& local_address) PURE;
};

using EnvoyQuicServerPreferredAddressConfigPtr =
    std::unique_ptr<EnvoyQuicServerPreferredAddressConfig>;

class EnvoyQuicServerPreferredAddressConfigFactory : public Config::TypedFactory {
public:
  std::string category() const override { return "envoy.quic.server_preferred_address"; }

  /**
   * Returns an EnvoyQuicServerPreferredAddressConfig object according to the given server preferred
   * address config.
   */
  virtual EnvoyQuicServerPreferredAddressConfigPtr
  createServerPreferredAddressConfig(const Protobuf::Message& config) PURE;
};

} // namespace Quic
} // namespace Envoy
