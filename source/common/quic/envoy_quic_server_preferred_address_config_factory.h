#pragma once

#include "envoy/config/typed_config.h"
#include "envoy/network/address.h"
#include "envoy/protobuf/message_validator.h"
#include "envoy/server/factory_context.h"

#include "quiche/quic/platform/api/quic_socket_address.h"

namespace Envoy {
namespace Quic {

// An interface to provide the server's preferred addresses at runtime.
class EnvoyQuicServerPreferredAddressConfig {
public:
  virtual ~EnvoyQuicServerPreferredAddressConfig() = default;

  // The set of addresses used to configure the server preferred addresses.
  struct Addresses {
    // Addresses that client is requested to use.
    quic::QuicSocketAddress ipv4_;
    quic::QuicSocketAddress ipv6_;

    // If destination NAT is applied between the client and Envoy, the addresses that
    // Envoy will see for client traffic to the server preferred address. If this is not
    // set, Envoy will expect to receive server preferred address traffic on the above addresses.
    //
    // A DNAT address will be ignored if the corresponding SPA address is not set.
    quic::QuicSocketAddress dnat_ipv4_;
    quic::QuicSocketAddress dnat_ipv6_;
  };

  /**
   * Called during config loading.
   * @param local_address the configured default listening address.
   * Returns a pair of the server preferred addresses in form of {IPv4, IPv6} which will be used for
   * the entire life time of the QUIC listener. An uninitialized address value means no preferred
   * address for that address family.
   */
  virtual Addresses
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
  createServerPreferredAddressConfig(const Protobuf::Message& config,
                                     ProtobufMessage::ValidationVisitor& validation_visitor,
                                     Server::Configuration::ServerFactoryContext& context) PURE;
};

} // namespace Quic
} // namespace Envoy
