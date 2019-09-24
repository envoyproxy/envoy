#include "extensions/quic_listeners/quiche/envoy_quic_client_session.h"

namespace Envoy {
namespace Quic {

EnvoyQuicClientSession::EnvoyQuicClientSession(
    const quic::QuicConfig& config, const quic::ParsedQuicVersionVector& supported_versions,
    std::unique_ptr<EnvoyQuicConnection> connection, const quic::QuicServerId& server_id,
    quic::QuicCryptoClientConfig* crypto_config,
    quic::QuicClientPushPromiseIndex* push_promise_index, Event::Dispatcher& dispatcher,
    uint32_t send_buffer_limit)
    : quic::QuicSpdyClientSession(config, supported_versions, connection.get(), server_id,
                                  crypto_config, push_promise_index),
      QuicFilterManagerConnectionImpl(std::move(connection), dispatcher, send_buffer_limit) {}

absl::string_view EnvoyQuicClientSession::requestedServerName() const {
  return {GetCryptoStream()->crypto_negotiated_params().sni};
}

void EnvoyQuicClientSession::connect() {
  Initialize();
  CryptoConnect();
  set_max_allowed_push_id(0u);
}

void EnvoyQuicClientSession::OnConnectionClosed(const quic::QuicConnectionCloseFrame& frame,
                                                quic::ConnectionCloseSource source) {
  quic::QuicSpdyClientSession::OnConnectionClosed(frame, source);
  onConnectionCloseEvent(frame, source);
}

void EnvoyQuicClientSession::Initialize() {
  quic::QuicSpdyClientSession::Initialize();
  quic_connection_->setEnvoyConnection(*this);
}

void EnvoyQuicClientSession::OnGoAway(const quic::QuicGoAwayFrame& frame) {
  ENVOY_CONN_LOG(debug, "GOAWAY received with error {}: {}", *this,
                 quic::QuicErrorCodeToString(frame.error_code), frame.reason_phrase);
  quic::QuicSpdyClientSession::OnGoAway(frame);
  if (http_connection_callbacks_ != nullptr) {
    http_connection_callbacks_->onGoAway();
  }
}

void EnvoyQuicClientSession::OnCryptoHandshakeEvent(CryptoHandshakeEvent event) {
  quic::QuicSpdyClientSession::OnCryptoHandshakeEvent(event);
  if (event == HANDSHAKE_CONFIRMED) {
    raiseEvent(Network::ConnectionEvent::Connected);
  }
}

std::unique_ptr<quic::QuicSpdyClientStream> EnvoyQuicClientSession::CreateClientStream() {
  auto stream = std::make_unique<EnvoyQuicClientStream>(GetNextOutgoingBidirectionalStreamId(),
                                                        this, quic::BIDIRECTIONAL);
  return stream;
}

quic::QuicSpdyStream* EnvoyQuicClientSession::CreateIncomingStream(quic::QuicStreamId id) {
  if (!ShouldCreateIncomingStream(id)) {
    return nullptr;
  }
  auto stream = new EnvoyQuicClientStream(id, this, quic::READ_UNIDIRECTIONAL);
  ActivateStream(std::unique_ptr<EnvoyQuicClientStream>(stream));
  return stream;
}

quic::QuicSpdyStream* EnvoyQuicClientSession::CreateIncomingStream(quic::PendingStream* pending) {
  auto stream = new EnvoyQuicClientStream(pending, this, quic::READ_UNIDIRECTIONAL);
  ActivateStream(std::unique_ptr<EnvoyQuicClientStream>(stream));
  return stream;
}

} // namespace Quic
} // namespace Envoy
