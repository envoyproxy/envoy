#include "source/common/quic/quic_client_packet_writer_factory_impl.h"

#include "source/common/network/udp_packet_writer_handler_impl.h"
#include "source/common/quic/envoy_quic_packet_writer.h"
#include "source/common/quic/envoy_quic_utils.h"

#include "absl/status/statusor.h"

namespace Envoy {
namespace Quic {

absl::StatusOr<QuicClientPacketWriterFactory::CreationResult>
QuicClientPacketWriterFactoryImpl::createSocketAndQuicPacketWriter(
    Network::Address::InstanceConstSharedPtr server_addr, quic::QuicNetworkHandle /*network*/,
    Network::Address::InstanceConstSharedPtr& local_addr,
    const Network::ConnectionSocket::OptionsSharedPtr& options) {
  absl::StatusOr<Network::ConnectionSocketPtr> connection_socket_or =
      createConnectionSocket(server_addr, local_addr, options);
  if (!connection_socket_or.ok()) {
    return connection_socket_or.status();
  }

  Network::ConnectionSocketPtr connection_socket = std::move(*connection_socket_or);

  Network::IoHandle& io_handle = connection_socket->ioHandle();

  return QuicClientPacketWriterFactory::CreationResult{
      std::make_unique<EnvoyQuicPacketWriter>(
          std::make_unique<Network::UdpDefaultWriter>(io_handle)),
      std::move(connection_socket)};
}

} // namespace Quic
} // namespace Envoy
