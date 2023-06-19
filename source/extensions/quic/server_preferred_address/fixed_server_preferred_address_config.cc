#include "source/extensions/quic/server_preferred_address/fixed_server_preferred_address_config.h"

namespace Envoy {
namespace Quic {

std::pair<quic::QuicSocketAddress, quic::QuicSocketAddress>
FixedServerPreferredAddressConfig::getServerPreferredAddresses(
    const Network::Address::InstanceConstSharedPtr& local_address) {
  int32_t port = local_address->ip()->port();
  return {quic::QuicSocketAddress(ip_v4_, port), quic::QuicSocketAddress(ip_v6_, port)};
}

Quic::EnvoyQuicServerPreferredAddressConfigPtr
FixedServerPreferredAddressConfigFactory::createServerPreferredAddressConfig(
    const Protobuf::Message& message, ProtobufMessage::ValidationVisitor& validation_visitor,
    ProcessContextOptRef /*context*/) {
  auto& config =
      MessageUtil::downcastAndValidate<const envoy::extensions::quic::server_preferred_address::v3::
                                           FixedServerPreferredAddressConfig&>(message,
                                                                               validation_visitor);
  quic::QuicIpAddress ip_v4, ip_v6;
  if (config.has_ipv4_address()) {
    if (!ip_v4.FromString(config.ipv4_address())) {
      ProtoExceptionUtil::throwProtoValidationException(
          absl::StrCat("bad v4 server preferred address: ", config.ipv4_address()), message);
    }
  }
  if (config.has_ipv6_address()) {
    if (!ip_v6.FromString(config.ipv6_address())) {
      ProtoExceptionUtil::throwProtoValidationException(
          absl::StrCat("bad v6 server preferred address: ", config.ipv6_address()), message);
    }
  }
  return std::make_unique<FixedServerPreferredAddressConfig>(ip_v4, ip_v6);
}

REGISTER_FACTORY(FixedServerPreferredAddressConfigFactory,
                 EnvoyQuicServerPreferredAddressConfigFactory);

} // namespace Quic
} // namespace Envoy
