#pragma once

#include "envoy/extensions/quic/server_preferred_address/v3/fixed_server_preferred_address_config.pb.h"
#include "envoy/extensions/quic/server_preferred_address/v3/fixed_server_preferred_address_config.pb.validate.h"
#include "envoy/registry/registry.h"

#include "source/common/quic/envoy_quic_server_preferred_address_config_factory.h"

namespace Envoy {
namespace Quic {

class FixedServerPreferredAddressConfig : public Quic::EnvoyQuicServerPreferredAddressConfig {
public:
  using QuicSocketOrIpAddress = absl::variant<quic::QuicSocketAddress, quic::QuicIpAddress>;

  FixedServerPreferredAddressConfig(const QuicSocketOrIpAddress& ipv4,
                                    const QuicSocketOrIpAddress& ipv6,
                                    const quic::QuicIpAddress ipv4_dnat,
                                    const quic::QuicIpAddress ipv6_dnat)
      : ip_v4_(ipv4), ip_v6_(ipv6), dnat_ip_v4_(ipv4_dnat), dnat_ip_v6_(ipv6_dnat) {}

  Addresses getServerPreferredAddresses(
      const Network::Address::InstanceConstSharedPtr& local_address) override;

private:
  const QuicSocketOrIpAddress ip_v4_;
  const QuicSocketOrIpAddress ip_v6_;
  const quic::QuicIpAddress dnat_ip_v4_;
  const quic::QuicIpAddress dnat_ip_v6_;
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
