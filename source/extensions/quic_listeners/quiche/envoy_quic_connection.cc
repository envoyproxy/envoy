#include "extensions/quic_listeners/quiche/envoy_quic_connection.h"

namespace Envoy {
namespace Quic {

EnvoyQuicConnection::EnvoyQuicConnection(quic::QuicConnectionId server_connection_id,
                                         quic::QuicSocketAddress initial_peer_address,
                                         quic::QuicConnectionHelperInterface* helper,
                                         quic::QuicAlarmFactory* alarm_factory,
                                         quic::QuicPacketWriter* writer, bool owns_writer,
                                         quic::Perspective perspective,
                                         const quic::ParsedQuicVersionVector& supported_versions)
    : quic::QuicConnection(server_connection_id, initial_peer_address, helper, alarm_factory,
                           writer, owns_writer, perspective, supported_versions) {}

} // namespace Quic
} // namespace Envoy
