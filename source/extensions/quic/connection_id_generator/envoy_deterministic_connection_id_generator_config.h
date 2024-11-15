#pragma once

#include "envoy/registry/registry.h"

#include "source/common/quic/envoy_quic_connection_id_generator_factory.h"

namespace Envoy {
namespace Quic {

class EnvoyDeterministicConnectionIdGeneratorConfigFactory
    : public EnvoyQuicConnectionIdGeneratorConfigFactory {
public:
  // EnvoyQuicConnectionIdGeneratorConfigFactory.
  ProtobufTypes::MessagePtr createEmptyConfigProto() override;
  EnvoyQuicConnectionIdGeneratorFactoryPtr
  createQuicConnectionIdGeneratorFactory(const Protobuf::Message& config) override;
  std::string name() const override { return "envoy.quic.deterministic_connection_id_generator"; }
};

DECLARE_FACTORY(EnvoyDeterministicConnectionIdGeneratorConfigFactory);

} // namespace Quic
} // namespace Envoy
