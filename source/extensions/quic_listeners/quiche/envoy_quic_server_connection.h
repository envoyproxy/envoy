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
                            Network::ListenerConfig& listener_config,
                            Server::ListenerStats& listener_stats, Network::Socket& listen_socket);

  // EnvoyQuicConnection
  // Overridden to set connection_socket_ with initialized self address and retrieve filter chain.
  bool OnPacketHeader(const quic::QuicPacketHeader& header) override;

private:
  Network::ListenerConfig& listener_config_;
  Server::ListenerStats& listener_stats_;
  // Latched to the corresponding quic FilterChain after connection_socket_ is
  // initialized.
  const Network::FilterChain* filter_chain_{nullptr};
};

} // namespace Quic
} // namespace Envoy
