#pragma once

#include "source/common/quic/envoy_quic_client_packet_writer_factory.h"

namespace Envoy {
namespace Quic {

class QuicPlatformPacketWriterFactory : public QuicClientPacketWriterFactory {
public:
  CreationResult createSocketAndQuicPacketWriter(
      Network::Address::InstanceConstSharedPtr server_addr, quic::QuicNetworkHandle network,
      Network::Address::InstanceConstSharedPtr& local_addr,
      const Network::ConnectionSocket::OptionsSharedPtr& options) override;

private:
  Network::ConnectionSocketPtr
  createConnectionSocketOnGivenNetwork(Network::Address::InstanceConstSharedPtr peer_addr,
                                       quic::QuicNetworkHandle network,
                                       Network::Address::InstanceConstSharedPtr& local_addr,
                                       const Network::ConnectionSocket::OptionsSharedPtr& options);
};

} // namespace Quic
} // namespace Envoy
