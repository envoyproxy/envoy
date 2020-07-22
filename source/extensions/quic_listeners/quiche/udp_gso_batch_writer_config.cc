#include "extensions/quic_listeners/quiche/udp_gso_batch_writer_config.h"

#include "envoy/config/core/v3/extension.pb.h"

#include "extensions/quic_listeners/quiche/udp_gso_batch_writer.h"

namespace Envoy {
namespace Quic {

ProtobufTypes::MessagePtr UdpGsoBatchWriterConfigFactory::createEmptyConfigProto() {
  return std::make_unique<envoy::config::core::v3::TypedExtensionConfig>();
}

Network::UdpPacketWriterFactoryPtr
UdpGsoBatchWriterConfigFactory::createUdpPacketWriterFactory(const Protobuf::Message& /*message*/) {
  return std::make_unique<UdpGsoBatchWriterFactory>();
}

std::string UdpGsoBatchWriterConfigFactory::name() const { return GsoBatchWriterName; }

REGISTER_FACTORY(UdpGsoBatchWriterConfigFactory, Network::UdpPacketWriterConfigFactory);

} // namespace Quic
} // namespace Envoy
