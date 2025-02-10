#include "source/extensions/quic/connection_id_generator/quic_lb/config.h"

#include "envoy/extensions/quic/connection_id_generator/quic_lb/v3/quic_lb.pb.h"

#include "source/extensions/quic/connection_id_generator/quic_lb/quic_lb.h"

namespace Envoy {
namespace Quic {
namespace Extensions {
namespace ConnectionIdGenerator {
namespace QuicLb {

ProtobufTypes::MessagePtr ConfigFactory::createEmptyConfigProto() {
  return std::make_unique<envoy::extensions::quic::connection_id_generator::quic_lb::v3::Config>();
}

EnvoyQuicConnectionIdGeneratorFactoryPtr ConfigFactory::createQuicConnectionIdGeneratorFactory(
    const Protobuf::Message& config, Server::Configuration::FactoryContext& context) {
  auto factory_or_status = Factory::create(
      dynamic_cast<const envoy::extensions::quic::connection_id_generator::quic_lb::v3::Config&>(
          config),
      context);
  THROW_IF_NOT_OK(factory_or_status.status());
  return std::move(factory_or_status.value());
}

REGISTER_FACTORY(ConfigFactory, EnvoyQuicConnectionIdGeneratorConfigFactory);

} // namespace QuicLb
} // namespace ConnectionIdGenerator
} // namespace Extensions
} // namespace Quic
} // namespace Envoy
