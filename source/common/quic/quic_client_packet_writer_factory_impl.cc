#include "source/common/quic/quic_client_packet_writer_factory_impl.h"

#include "source/common/network/udp_packet_writer_handler_impl.h"
#include "source/common/quic/envoy_quic_packet_writer.h"
#include "source/common/quic/envoy_quic_utils.h"

namespace Envoy {
namespace Quic {

QuicClientPacketWriterFactory::CreationResult
QuicClientPacketWriterFactoryImpl::createSocketAndQuicPacketWriter(
    Network::Address::InstanceConstSharedPtr server_addr, quic::QuicNetworkHandle /*network*/,
    Network::Address::InstanceConstSharedPtr& local_addr,
    const Network::ConnectionSocket::OptionsSharedPtr& options) {
  Network::ConnectionSocketPtr connection_socket =
      createConnectionSocket(server_addr, local_addr, options);
  Network::IoHandle& io_handle = connection_socket->ioHandle();
  return {std::make_unique<EnvoyQuicPacketWriter>(
              std::make_unique<Network::UdpDefaultWriter>(io_handle)),
          std::move(connection_socket)};
}

} // namespace Quic
} // namespace Envoy
