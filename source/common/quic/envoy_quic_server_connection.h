#pragma once

#include "envoy/network/listener.h"

#include "common/quic/envoy_quic_utils.h"
#include "common/quic/quic_network_connection.h"

#include "server/connection_handler_impl.h"

#define QUICHE_INCLUDE_1 "quiche/quic/core/quic_connection.h"
#include "common/quic/quic_includes_ignores.h"

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
                            Network::ConnectionSocketPtr connection_socket);

  // QuicNetworkConnection
  // Overridden to set connection_socket_ with initialized self address and retrieve filter chain.
  bool OnPacketHeader(const quic::QuicPacketHeader& header) override;
};

} // namespace Quic
} // namespace Envoy
