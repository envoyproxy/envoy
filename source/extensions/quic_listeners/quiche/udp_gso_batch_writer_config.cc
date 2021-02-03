#include "extensions/quic_listeners/quiche/udp_gso_batch_writer_config.h"

#include "envoy/config/listener/v3/udp_gso_batch_writer_config.pb.h"

#include "common/api/os_sys_calls_impl.h"

#include "extensions/quic_listeners/quiche/udp_gso_batch_writer.h"

namespace Envoy {
namespace Quic {

ProtobufTypes::MessagePtr UdpGsoBatchWriterConfigFactory::createEmptyConfigProto() {
  return std::make_unique<envoy::config::listener::v3::UdpGsoBatchWriterOptions>();
}

Network::UdpPacketWriterFactoryPtr
UdpGsoBatchWriterConfigFactory::createUdpPacketWriterFactory(const Protobuf::Message& /*message*/) {
  if (!Api::OsSysCallsSingleton::get().supportsUdpGso()) {
    throw EnvoyException("Error configuring batch writer on platform without support "
                         "for UDP GSO. Reset udp_writer_config to default writer");
  }

#if UDP_GSO_BATCH_WRITER_COMPILETIME_SUPPORT
  return std::make_unique<UdpGsoBatchWriterFactory>();
#else
  // On non-linux, `supportsUdpGso()` always returns false.
  NOT_REACHED_GCOVR_EXCL_LINE;
#endif
}

std::string UdpGsoBatchWriterConfigFactory::name() const { return GsoBatchWriterName; }

REGISTER_FACTORY(UdpGsoBatchWriterConfigFactory, Network::UdpPacketWriterConfigFactory);

} // namespace Quic
} // namespace Envoy
