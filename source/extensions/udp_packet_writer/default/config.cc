#include "source/extensions/udp_packet_writer/default/config.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/network/utility.h"

namespace Envoy {
namespace Network {

REGISTER_FACTORY(UdpDefaultWriterFactoryFactory, UdpPacketWriterFactoryFactory);

} // namespace Network
} // namespace Envoy
