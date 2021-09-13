#pragma once

#include "envoy/event/dispatcher.h"

#include "source/common/network/utility.h"
#include "source/common/quic/envoy_quic_utils.h"
#include "source/common/quic/quic_network_connection.h"

namespace Envoy {
namespace Quic {

class EnvoyQuicPathValidationContext : public quic::QuicPathValidationContext {
public:
  EnvoyQuicPathValidationContext(quic::QuicSocketAddress& self_address, quic::QuicSocketAddress& peer_address, 
    Network::ConnectionSocketPtr connectionSocket, std::unique_ptr<EnvoyQuicPacketWriter> writer);

  ~EnvoyQuicPathValidationContext() override;

  quic::QuicPacketWriter* WriterToUse() override;

  std::unique_ptr<EnvoyQuicPacketWriter> ReleaseWriter();
  Network::ConnectionSocketPtr connectionSocket ReleaseSocket();


private:
  Network::ConnectionSocketPtr connectionSocket_;
  std::unique_ptr<EnvoyQuicPacketWriter> writer_;
};

} // namespace Quic
} // namespace Envoy
