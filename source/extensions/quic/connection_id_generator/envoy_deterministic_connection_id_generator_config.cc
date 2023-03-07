#include "source/extensions/quic/connection_id_generator/envoy_deterministic_connection_id_generator_config.h"

#include "envoy/extensions/quic/connection_id_generator/v3/envoy_deterministic_connection_id_generator.pb.h"

#include "source/extensions/quic/connection_id_generator/envoy_deterministic_connection_id_generator.h"

namespace Envoy {
namespace Quic {

ProtobufTypes::MessagePtr
EnvoyDeterministicConnectionIdGeneratorConfigFactory::createEmptyConfigProto() {
  return std::make_unique<envoy::extensions::quic::connection_id_generator::v3::
                              DeterministicConnectionIdGeneratorConfig>();
}

EnvoyQuicConnectionIdGeneratorFactoryPtr
EnvoyDeterministicConnectionIdGeneratorConfigFactory::createQuicConnectionIdGeneratorFactory(
    const Protobuf::Message&) {
  return std::make_unique<EnvoyDeterministicConnectionIdGeneratorFactory>();
}

REGISTER_FACTORY(EnvoyDeterministicConnectionIdGeneratorConfigFactory,
                 EnvoyQuicConnectionIdGeneratorConfigFactory);

} // namespace Quic
} // namespace Envoy
