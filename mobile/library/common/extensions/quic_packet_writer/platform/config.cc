#include "library/common/extensions/quic_packet_writer/platform/config.h"

#include "library/common/extensions/quic_packet_writer/platform/platform_packet_writer_factory.h"

namespace Envoy {
namespace Quic {

Envoy::Quic::QuicClientPacketWriterFactoryPtr
QuicPlatformPacketWriterConfigFactory::createQuicClientPacketWriterFactory(
    const Protobuf::Message& /*config*/, Event::Dispatcher& dispatcher,
    Envoy::ProtobufMessage::ValidationVisitor& /*validation_visitor*/) {
  return std::make_unique<QuicPlatformPacketWriterFactory>(dispatcher);
}

/**
 * Static registration for the platform packet writer factory.
 * @see RegistryFactory.
 */
REGISTER_FACTORY(QuicPlatformPacketWriterConfigFactory, QuicClientPacketWriterConfigFactory);

} // namespace Quic
} // namespace Envoy
