#include "source/common/quic/quic_client_packet_writer_factory_impl.h"

#include "source/common/network/io_socket_handle_impl.h"
#include "source/common/network/udp_packet_writer_handler_impl.h"
#include "source/common/quic/envoy_quic_packet_writer.h"
#include "source/common/quic/envoy_quic_utils.h"

#include "absl/status/statusor.h"

namespace Envoy {
namespace Quic {

QuicClientPacketWriterFactory::CreationResult
QuicClientPacketWriterFactoryImpl::createSocketAndQuicPacketWriter(
    Network::Address::InstanceConstSharedPtr server_addr, quic::QuicNetworkHandle /*network*/,
    Network::Address::InstanceConstSharedPtr& local_addr,
    const Network::ConnectionSocket::OptionsSharedPtr& options) {
  absl::StatusOr<Network::ConnectionSocketPtr> connection_socket_or =
      createConnectionSocket(server_addr, local_addr, options);

  Network::ConnectionSocketPtr connection_socket =
      connection_socket_or.ok() ? std::move(*connection_socket_or)
                                : std::make_unique<Network::ConnectionSocketImpl>(
                                      std::make_unique<Network::IoSocketHandleImpl>(INVALID_SOCKET),
                                      local_addr, server_addr);

  Network::IoHandle& io_handle = connection_socket->ioHandle();
  return {std::make_unique<EnvoyQuicPacketWriter>(
              std::make_unique<Network::UdpDefaultWriter>(io_handle)),
          std::move(connection_socket)};
}

} // namespace Quic
} // namespace Envoy
