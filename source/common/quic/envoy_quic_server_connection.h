#pragma once

#include "envoy/network/listener.h"

#include "source/common/quic/envoy_quic_utils.h"
#include "source/common/quic/quic_network_connection.h"
#include "source/server/connection_handler_impl.h"

#include "quiche/quic/core/quic_connection.h"

namespace Envoy {
namespace Quic {

class EnvoyQuicServerConnection : public quic::QuicConnection, public QuicNetworkConnection {
public:
  EnvoyQuicServerConnection(const quic::QuicConnectionId& server_connection_id,
                            quic::QuicSocketAddress initial_self_address,
                            quic::QuicSocketAddress initial_peer_address,
                            quic::QuicConnectionHelperInterface& helper,
                            quic::QuicAlarmFactory& alarm_factory, quic::QuicPacketWriter* writer,
                            bool owns_writer,
                            const quic::ParsedQuicVersionVector& supported_versions,
                            Network::ConnectionSocketPtr connection_socket,
                            quic::ConnectionIdGeneratorInterface& generator);

  // quic::QuicConnection
  // Overridden to set connection_socket_ with initialized self address and retrieve filter chain.
  bool OnPacketHeader(const quic::QuicPacketHeader& header) override;
  void OnCanWrite() override;

  bool deferSend() const { return defer_send_; }

  bool actuallyDeferSend() const { return defer_send_in_response_to_packets(); }

private:
  const bool defer_send_;
};

// An implementation that issues connection IDs with stable first 4 types.
class EnvoyQuicSelfIssuedConnectionIdManager : public quic::QuicSelfIssuedConnectionIdManager {
public:
  using QuicSelfIssuedConnectionIdManager::QuicSelfIssuedConnectionIdManager;
};

} // namespace Quic
} // namespace Envoy
