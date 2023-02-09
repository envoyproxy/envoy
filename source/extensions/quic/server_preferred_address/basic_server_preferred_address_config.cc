#include "source/extensions/quic/server_preferred_address/basic_server_preferred_address_config.h"

namespace Envoy {
namespace Quic {

std::pair<quic::QuicSocketAddress, quic::QuicSocketAddress>
BasicServerPreferredAddressConfig::getServerPreferredAddresses(
    const Network::Address::InstanceConstSharedPtr& local_address) {
  int32_t port = local_address->ip()->port();
  return {quic::QuicSocketAddress(ip_v4_, port), quic::QuicSocketAddress(ip_v6_, port)};
}

Quic::EnvoyQuicServerPreferredAddressConfigPtr
BasicServerPreferredAddressConfigFactory::createServerPreferredAddressConfig(
    const Protobuf::Message& message, ProtobufMessage::ValidationVisitor& validation_visitor) {
  auto& config =
      MessageUtil::downcastAndValidate<const envoy::extensions::quic::server_preferred_address::v3::
                                           BasicServerPreferredAddressConfig&>(message,
                                                                               validation_visitor);
  quic::QuicIpAddress ip_v4, ip_v6;
  const std::string err("bad server preferred address");
  if (!config.ipv4_address().empty()) {
    if (!ip_v4.FromString(config.ipv4_address())) {
      ProtoExceptionUtil::throwProtoValidationException(err, message);
    }
  }
  if (!config.ipv6_address().empty()) {
    if (!ip_v6.FromString(config.ipv6_address())) {
      ProtoExceptionUtil::throwProtoValidationException(err, message);
    }
  }
  return std::make_unique<BasicServerPreferredAddressConfig>(ip_v4, ip_v6);
}

REGISTER_FACTORY(BasicServerPreferredAddressConfigFactory,
                 EnvoyQuicServerPreferredAddressConfigFactory);

} // namespace Quic
} // namespace Envoy
