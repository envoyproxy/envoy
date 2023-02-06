#include "source/extensions/quic/server_preferred_address/empty_server_preferred_address_config.h"

#include "envoy/extensions/quic/server_preferred_address/v3/empty_server_preferred_address_config.pb.h"

namespace Envoy {
namespace Quic {

ProtobufTypes::MessagePtr EmptyServerPreferredAddressConfigFactory::createEmptyConfigProto() {
  return std::make_unique<
      envoy::extensions::quic::server_preferred_address::v3::EmptyServerPreferredAddressConfig>();
}

EnvoyQuicServerPreferredAddressConfigPtr
EmptyServerPreferredAddressConfigFactory::createServerPreferredAddressConfig(
    const Protobuf::Message&) {
  return std::make_unique<EmptyServerPreferredAddressConfig>();
}

REGISTER_FACTORY(EmptyServerPreferredAddressConfigFactory,
                 EnvoyQuicServerPreferredAddressConfigFactory);

} // namespace Quic
} // namespace Envoy
