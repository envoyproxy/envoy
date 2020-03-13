#include "extensions/quic_listeners/quiche/envoy_quic_connection.h"

#include "extensions/quic_listeners/quiche/envoy_quic_utils.h"

namespace Envoy {
namespace Quic {

EnvoyQuicConnection::EnvoyQuicConnection(const quic::QuicConnectionId& server_connection_id,
                                         quic::QuicSocketAddress initial_peer_address,
                                         quic::QuicConnectionHelperInterface& helper,
                                         quic::QuicAlarmFactory& alarm_factory,
                                         quic::QuicPacketWriter* writer, bool owns_writer,
                                         quic::Perspective perspective,
                                         const quic::ParsedQuicVersionVector& supported_versions,
                                         Network::ConnectionSocketPtr&& connection_socket)
    : quic::QuicConnection(server_connection_id, initial_peer_address, &helper, &alarm_factory,
                           writer, owns_writer, perspective, supported_versions),
      connection_socket_(std::move(connection_socket)) {}

EnvoyQuicConnection::~EnvoyQuicConnection() { connection_socket_->close(); }

uint64_t EnvoyQuicConnection::id() const { return envoy_connection_->id(); }

} // namespace Quic
} // namespace Envoy
