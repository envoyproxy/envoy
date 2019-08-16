#include "extensions/quic_listeners/quiche/envoy_quic_dispatcher.h"

#include "extensions/quic_listeners/quiche/envoy_quic_server_session.h"

namespace Envoy {
namespace Quic {

EnvoyQuicDispatcher::EnvoyQuicDispatcher(
    const quic::QuicCryptoServerConfig* crypto_config, quic::QuicVersionManager* version_manager,
    std::unique_ptr<quic::QuicConnectionHelperInterface> helper,
    std::unique_ptr<quic::QuicAlarmFactory> alarm_factory,
    uint8_t expected_server_connection_id_length, Server::ConnectionHandlerImpl& connection_handler,
    Network::ListenerConfig& listener_config, Server::ListenerStats& listener_stats)
    : quic::QuicDispatcher(&quic_config_, crypto_config, version_manager, std::move(helper),
                           std::make_unique<EnvoyQuicCryptoServerStreamHelper>(),
                           std::move(alarm_factory), expected_server_connection_id_length),
      connection_handler_(connection_handler), listener_config_(listener_config),
      listener_stats_(listener_stats) {
  // Turn off chlo buffering in QuicDispatcher because per event loop clean
  // up is not implemented.
  // TODO(danzh): Add a per event loop callback to
  // Network::UdpListenerCallbacks which should be called at the beginning
  // of HandleReadEvent(). And this callback should call quic::Dispatcher::ProcessBufferedChlos().
  SetQuicFlag(FLAGS_quic_allow_chlo_buffering, false);
}

void EnvoyQuicDispatcher::OnConnectionClosed(quic::QuicConnectionId connection_id,
                                             quic::QuicErrorCode error,
                                             const std::string& error_details,
                                             quic::ConnectionCloseSource source) {
  quic::QuicDispatcher::OnConnectionClosed(connection_id, error, error_details, source);
  ASSERT(connection_handler_.num_connections_ > 0);
  --connection_handler_.num_connections_;
}

quic::QuicSession* EnvoyQuicDispatcher::CreateQuicSession(
    quic::QuicConnectionId server_connection_id, const quic::QuicSocketAddress& peer_address,
    quic::QuicStringPiece /*alpn*/, const quic::ParsedQuicVersion& version) {
  auto quic_connection = new EnvoyQuicConnection(
      server_connection_id, peer_address, *helper(), *alarm_factory(), *writer(),
      /*owns_writer=*/false, quic::Perspective::IS_SERVER, quic::ParsedQuicVersionVector{version},
      listener_config_, listener_stats_);
  auto quic_session = new EnvoyQuicServerSession(
      config(), quic::ParsedQuicVersionVector{version}, quic_connection, this, session_helper(),
      crypto_config(), compressed_certs_cache(), connection_handler_.dispatcher_,
      listener_config_.perConnectionBufferLimitBytes());
  quic_session->Initialize();
  // Filter chain can't be retrieved here as self address is unknown at this
  // point.
  ++connection_handler_.num_connections_;
  return quic_session;
}

} // namespace Quic
} // namespace Envoy
