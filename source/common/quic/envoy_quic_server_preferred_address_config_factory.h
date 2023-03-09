#pragma once

#include "envoy/config/typed_config.h"
#include "envoy/network/address.h"
#include "envoy/protobuf/message_validator.h"
#include "envoy/server/process_context.h"

#include "quiche/quic/platform/api/quic_socket_address.h"

namespace Envoy {
namespace Quic {

// An interface to provide the server's preferred addresses at runtime.
class EnvoyQuicServerPreferredAddressConfig {
public:
  virtual ~EnvoyQuicServerPreferredAddressConfig() = default;

  /**
   * Called during config loading.
   * @param local_address the configured default listening address.
   * Returns a pair of the server preferred addresses in form of {IPv4, IPv6} which will be used for
   * the entire life time of the QUIC listener. An uninitialized address value means no preferred
   * address for that address family.
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
  createServerPreferredAddressConfig(const Protobuf::Message& config,
                                     ProtobufMessage::ValidationVisitor& validation_visitor,
                                     ProcessContextOptRef context) PURE;
};

} // namespace Quic
} // namespace Envoy
