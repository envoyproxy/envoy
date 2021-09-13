#include "envoy_quic_path_validation_context.h"


EnvoyQuicPathValidationContext::EnvoyQuicPathValidationContext(quic::QuicSocketAddress& self_address, quic::QuicSocketAddress& peer_address, 
    Network::ConnectionSocketPtr connectionSocket, std::unique_ptr<EnvoyQuicPacketWriter> writer) : QuicPathValidationContext(self_address, peer_address),
      socket_(std::move(connectionSocket)), writer_(std::move(writer)) {}

~EnvoyQuicPathValidationContext::EnvoyQuicPathValidationContext() = default;

quic::QuicPacketWriter* EnvoyQuicPathValidationContext::WriterToUse() {
  return 
}
