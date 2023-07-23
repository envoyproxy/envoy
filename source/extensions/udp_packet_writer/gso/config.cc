#include "source/extensions/udp_packet_writer/gso/config.h"

namespace Envoy {
namespace Quic {

#if UDP_GSO_BATCH_WRITER_COMPILETIME_SUPPORT

REGISTER_FACTORY(UdpGsoBatchWriterFactoryFactory, Network::UdpPacketWriterFactoryFactory);

#endif

} // namespace Quic
} // namespace Envoy
