#include "library/common/extensions/quic_packet_writer/platform/platform_packet_writer_factory.h"

#include "source/common/network/udp_packet_writer_handler_impl.h"
#include "source/common/quic/envoy_quic_packet_writer.h"
#include "source/common/quic/envoy_quic_utils.h"

#include "library/common/system/system_helper.h"

namespace Envoy {
namespace Quic {

QuicClientPacketWriterFactory::CreationResult
QuicPlatformPacketWriterFactory::createSocketAndQuicPacketWriter(
    Network::Address::InstanceConstSharedPtr server_addr, quic::QuicNetworkHandle network,
    Network::Address::InstanceConstSharedPtr& local_addr,
    const Network::ConnectionSocket::OptionsSharedPtr& options) {
  auto custom_bind_func = [](Network::ConnectionSocket& socket, quic::QuicNetworkHandle net) {
    SystemHelper::getInstance().bindSocketToNetwork(socket, net);
  };

  Network::ConnectionSocketPtr connection_socket =
      createConnectionSocket(server_addr, local_addr, options, network, custom_bind_func);

  Network::IoHandle& io_handle = connection_socket->ioHandle();
  return {std::make_unique<EnvoyQuicPacketWriter>(
              std::make_unique<Network::UdpDefaultWriter>(io_handle)),
          std::move(connection_socket)};
}

} // namespace Quic
} // namespace Envoy
