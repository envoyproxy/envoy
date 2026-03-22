#pragma once

#include "source/common/quic/envoy_quic_client_packet_writer_factory.h"

namespace Envoy {
namespace Quic {

// This extension supports creating a UDP socket binding to a platform-specific network handle.
// On Android M+, the handle is associated with Network.getNetworkHandle().
class QuicPlatformPacketWriterFactory : public QuicClientPacketWriterFactory {
public:
  QuicPlatformPacketWriterFactory(Event::Dispatcher& dispatcher) : dispatcher_(dispatcher) {}

  CreationResult createSocketAndQuicPacketWriter(
      Network::Address::InstanceConstSharedPtr server_addr, quic::QuicNetworkHandle network,
      Network::Address::InstanceConstSharedPtr& local_addr,
      const Network::ConnectionSocket::OptionsSharedPtr& options) override;

private:
  Event::Dispatcher& dispatcher_;
};

} // namespace Quic
} // namespace Envoy
