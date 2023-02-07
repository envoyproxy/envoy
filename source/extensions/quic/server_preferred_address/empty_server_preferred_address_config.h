#pragma once

#include "envoy/registry/registry.h"

#include "source/common/quic/envoy_quic_server_preferred_address_config_factory.h"

namespace Envoy {
namespace Quic {

class EmptyServerPreferredAddressConfig : public EnvoyQuicServerPreferredAddressConfig {
public:
  std::pair<quic::QuicSocketAddress, quic::QuicSocketAddress>
  getServerPreferredAddresses(const Network::Address::InstanceConstSharedPtr&) override {
    return {quic::QuicSocketAddress(), quic::QuicSocketAddress()};
  }
};

class EmptyServerPreferredAddressConfigFactory
    : public EnvoyQuicServerPreferredAddressConfigFactory {
public:
  // EnvoyQuicConnectionIdGeneratorConfigFactory.
  ProtobufTypes::MessagePtr createEmptyConfigProto() override;
  EnvoyQuicServerPreferredAddressConfigPtr createServerPreferredAddressConfig(
      const Protobuf::Message& config,
      ProtobufMessage::ValidationVisitor& validation_visitor) override;

  std::string name() const override { return "envoy.quic.empty_server_preferred_address"; }
};

DECLARE_FACTORY(EmptyServerPreferredAddressConfigFactory);

} // namespace Quic
} // namespace Envoy
