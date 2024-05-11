#pragma once

#include "envoy/extensions/quic/server_preferred_address/v3/fixed_server_preferred_address_config.pb.h"
#include "envoy/extensions/quic/server_preferred_address/v3/fixed_server_preferred_address_config.pb.validate.h"
#include "envoy/registry/registry.h"

#include "source/common/quic/envoy_quic_server_preferred_address_config_factory.h"

namespace Envoy {
namespace Quic {

class FixedServerPreferredAddressConfig : public Quic::EnvoyQuicServerPreferredAddressConfig {
public:
  struct FamilyAddresses {
    quic::QuicSocketAddress spa_;
    quic::QuicIpAddress dnat_;
  };

  FixedServerPreferredAddressConfig(const FamilyAddresses& v4, const FamilyAddresses& v6)
      : v4_(v4), v6_(v6) {}

  Addresses getServerPreferredAddresses(
      const Network::Address::InstanceConstSharedPtr& local_address) override;

private:
  const FamilyAddresses v4_;
  const FamilyAddresses v6_;
};

class FixedServerPreferredAddressConfigFactory
    : public Quic::EnvoyQuicServerPreferredAddressConfigFactory {
public:
  std::string name() const override { return "quic.server_preferred_address.fixed"; }

  Quic::EnvoyQuicServerPreferredAddressConfigPtr
  createServerPreferredAddressConfig(const Protobuf::Message& message,
                                     ProtobufMessage::ValidationVisitor& validation_visitor,
                                     ProcessContextOptRef context) override;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return ProtobufTypes::MessagePtr{new envoy::extensions::quic::server_preferred_address::v3::
                                         FixedServerPreferredAddressConfig()};
  }
};

DECLARE_FACTORY(FixedServerPreferredAddressConfigFactory);

} // namespace Quic
} // namespace Envoy
