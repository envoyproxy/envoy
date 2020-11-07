#include "envoy/network/listener.h"

#include "server/connection_handler_impl.h"

#include "extensions/quic_listeners/quiche/envoy_quic_connection.h"

namespace Envoy {
namespace Quic {

class EnvoyQuicServerConnection : public EnvoyQuicConnection {
public:
  EnvoyQuicServerConnection(const quic::QuicConnectionId& server_connection_id,
                            quic::QuicSocketAddress initial_peer_address,
                            quic::QuicConnectionHelperInterface& helper,
                            quic::QuicAlarmFactory& alarm_factory, quic::QuicPacketWriter* writer,
                            bool owns_writer,
                            const quic::ParsedQuicVersionVector& supported_versions,
                            Network::Socket& listen_socket);

  // EnvoyQuicConnection
  // Overridden to set connection_socket_ with initialized self address and retrieve filter chain.
  bool OnPacketHeader(const quic::QuicPacketHeader& header) override;
};

} // namespace Quic
} // namespace Envoy
