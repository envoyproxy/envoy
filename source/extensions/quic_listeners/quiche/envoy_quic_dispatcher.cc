#include "extensions/quic_listeners/quiche/envoy_quic_dispatcher.h"

#include "extensions/quic_listeners/quiche/envoy_quic_server_connection.h"
#include "extensions/quic_listeners/quiche/envoy_quic_server_session.h"

namespace Envoy {
namespace Quic {

EnvoyQuicDispatcher::EnvoyQuicDispatcher(
    const quic::QuicCryptoServerConfig* crypto_config, const quic::QuicConfig& quic_config,
    quic::QuicVersionManager* version_manager,
    std::unique_ptr<quic::QuicConnectionHelperInterface> helper,
    std::unique_ptr<quic::QuicAlarmFactory> alarm_factory,
    uint8_t expected_server_connection_id_length, Network::ConnectionHandler& connection_handler,
    Network::ListenerConfig& listener_config, Server::ListenerStats& listener_stats,
    Event::Dispatcher& dispatcher, Network::Socket& listen_socket)
    : quic::QuicDispatcher(&quic_config, crypto_config, version_manager, std::move(helper),
                           std::make_unique<EnvoyQuicCryptoServerStreamHelper>(),
                           std::move(alarm_factory), expected_server_connection_id_length),
      connection_handler_(connection_handler), listener_config_(listener_config),
      listener_stats_(listener_stats), dispatcher_(dispatcher), listen_socket_(listen_socket) {
  // Turn off chlo buffering in QuicDispatcher because per event loop clean
  // up is not implemented.
  // TODO(danzh): Add a per event loop callback to
  // Network::UdpListenerCallbacks which should be called at the beginning
  // of HandleReadEvent(). And this callback should call quic::Dispatcher::ProcessBufferedChlos().
  SetQuicFlag(FLAGS_quic_allow_chlo_buffering, false);
  // Set send buffer twice of max flow control window to ensure that stream send
  // buffer always takes all the data.
  // The max amount of data buffered is the per-stream high watermark + the max
  // flow control window of upstream. The per-stream high watermark should be
  // smaller than max flow control window to make sure upper stream can be flow
  // control blocked early enough not to send more than the threshold allows.
  // TODO(#8826) Ideally we should use the negotiated value from upstream which is not accessible
  // for now. 512MB is way to large, but the actual bytes buffered should be bound by the negotiated
  // upstream flow control window.
  SetQuicFlag(FLAGS_quic_buffered_data_threshold,
              2 * Http::Http2Settings::DEFAULT_INITIAL_STREAM_WINDOW_SIZE); // 512MB
}

void EnvoyQuicDispatcher::OnConnectionClosed(quic::QuicConnectionId connection_id,
                                             quic::QuicErrorCode error,
                                             const std::string& error_details,
                                             quic::ConnectionCloseSource source) {
  quic::QuicDispatcher::OnConnectionClosed(connection_id, error, error_details, source);
  connection_handler_.decNumConnections();
}

std::unique_ptr<quic::QuicSession> EnvoyQuicDispatcher::CreateQuicSession(
    quic::QuicConnectionId server_connection_id, const quic::QuicSocketAddress& peer_address,
    quiche::QuicheStringPiece /*alpn*/, const quic::ParsedQuicVersion& version) {
  auto quic_connection = std::make_unique<EnvoyQuicServerConnection>(
      server_connection_id, peer_address, *helper(), *alarm_factory(), writer(),
      /*owns_writer=*/false, quic::ParsedQuicVersionVector{version}, listener_config_,
      listener_stats_, listen_socket_);
  auto quic_session = std::make_unique<EnvoyQuicServerSession>(
      config(), quic::ParsedQuicVersionVector{version}, std::move(quic_connection), this,
      session_helper(), crypto_config(), compressed_certs_cache(), dispatcher_,
      listener_config_.perConnectionBufferLimitBytes());
  quic_session->Initialize();
  // Filter chain can't be retrieved here as self address is unknown at this
  // point.
  // TODO(danzh): change QUIC interface to pass in self address as it is already
  // known. In this way, filter chain can be retrieved at this point. But one
  // thing to pay attention is that if the retrieval fails, connection needs to
  // be closed, and it should be added to time wait list instead of session map.
  connection_handler_.incNumConnections();
  return quic_session;
}

} // namespace Quic
} // namespace Envoy
