#pragma once

#include "envoy/registry/registry.h"

#include "source/common/quic/envoy_quic_connection_id_generator_factory.h"

namespace Envoy {
namespace Quic {
namespace Extensions {
namespace ConnectionIdGenerator {
namespace Deterministic {

class EnvoyDeterministicConnectionIdGeneratorConfigFactory
    : public Quic::EnvoyQuicConnectionIdGeneratorConfigFactory {
public:
  // EnvoyQuicConnectionIdGeneratorConfigFactory.
  ProtobufTypes::MessagePtr createEmptyConfigProto() override;
  Quic::EnvoyQuicConnectionIdGeneratorFactoryPtr
  createQuicConnectionIdGeneratorFactory(const Protobuf::Message& config,
                                         ProtobufMessage::ValidationVisitor& validation_visitor,
                                         Server::Configuration::FactoryContext&) override;
  std::string name() const override { return "envoy.quic.deterministic_connection_id_generator"; }
};

DECLARE_FACTORY(EnvoyDeterministicConnectionIdGeneratorConfigFactory);

} // namespace Deterministic
} // namespace ConnectionIdGenerator
} // namespace Extensions
} // namespace Quic
} // namespace Envoy
