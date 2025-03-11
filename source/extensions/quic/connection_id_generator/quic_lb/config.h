#pragma once

#include "envoy/registry/registry.h"

#include "source/common/quic/envoy_quic_connection_id_generator_factory.h"

namespace Envoy {
namespace Quic {
namespace Extensions {
namespace ConnectionIdGenerator {
namespace QuicLb {

class ConfigFactory : public Quic::EnvoyQuicConnectionIdGeneratorConfigFactory {
public:
  // EnvoyQuicConnectionIdGeneratorConfigFactory.
  ProtobufTypes::MessagePtr createEmptyConfigProto() override;
  Quic::EnvoyQuicConnectionIdGeneratorFactoryPtr
  createQuicConnectionIdGeneratorFactory(const Protobuf::Message& config,
                                         ProtobufMessage::ValidationVisitor& validation_visitor,
                                         Server::Configuration::FactoryContext& context) override;
  std::string name() const override { return "envoy.quic.connection_id_generator.quic_lb"; }
};

DECLARE_FACTORY(ConfigFactory);

} // namespace QuicLb
} // namespace ConnectionIdGenerator
} // namespace Extensions
} // namespace Quic
} // namespace Envoy
