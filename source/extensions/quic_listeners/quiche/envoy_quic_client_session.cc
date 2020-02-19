#include "extensions/quic_listeners/quiche/envoy_quic_client_session.h"

namespace Envoy {
namespace Quic {

EnvoyQuicClientSession::EnvoyQuicClientSession(
    const quic::QuicConfig& config, const quic::ParsedQuicVersionVector& supported_versions,
    std::unique_ptr<EnvoyQuicClientConnection> connection, const quic::QuicServerId& server_id,
    quic::QuicCryptoClientConfig* crypto_config,
    quic::QuicClientPushPromiseIndex* push_promise_index, Event::Dispatcher& dispatcher,
    uint32_t send_buffer_limit)
    : QuicFilterManagerConnectionImpl(*connection, dispatcher, send_buffer_limit),
      quic::QuicSpdyClientSession(config, supported_versions, connection.release(), server_id,
                                  crypto_config, push_promise_index) {}

EnvoyQuicClientSession::~EnvoyQuicClientSession() {
  ASSERT(!connection()->connected());
  quic_connection_ = nullptr;
}

absl::string_view EnvoyQuicClientSession::requestedServerName() const {
  return {GetCryptoStream()->crypto_negotiated_params().sni};
}

void EnvoyQuicClientSession::connect() {
  dynamic_cast<EnvoyQuicClientConnection*>(quic_connection_)->setUpConnectionSocket();
  // Start version negotiation and crypto handshake during which the connection may fail if server
  // doesn't support the one and only supported version.
  CryptoConnect();
  SetMaxAllowedPushId(0u);
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

void EnvoyQuicClientSession::OnCanWrite() {
  quic::QuicSpdyClientSession::OnCanWrite();
  maybeApplyDelayClosePolicy();
}

void EnvoyQuicClientSession::OnGoAway(const quic::QuicGoAwayFrame& frame) {
  ENVOY_CONN_LOG(debug, "GOAWAY received with error {}: {}", *this,
                 quic::QuicErrorCodeToString(frame.error_code), frame.reason_phrase);
  quic::QuicSpdyClientSession::OnGoAway(frame);
  if (http_connection_callbacks_ != nullptr) {
    http_connection_callbacks_->onGoAway();
  }
}

void EnvoyQuicClientSession::SetDefaultEncryptionLevel(quic::EncryptionLevel level) {
  quic::QuicSpdyClientSession::SetDefaultEncryptionLevel(level);
  if (level == quic::ENCRYPTION_FORWARD_SECURE) {
    // This is only reached once, when handshake is done.
    raiseConnectionEvent(Network::ConnectionEvent::Connected);
  }
}

std::unique_ptr<quic::QuicSpdyClientStream> EnvoyQuicClientSession::CreateClientStream() {
  return std::make_unique<EnvoyQuicClientStream>(GetNextOutgoingBidirectionalStreamId(), this,
                                                 quic::BIDIRECTIONAL);
}

quic::QuicSpdyStream* EnvoyQuicClientSession::CreateIncomingStream(quic::QuicStreamId /*id*/) {
  // Disallow server initiated stream.
  NOT_REACHED_GCOVR_EXCL_LINE;
}

quic::QuicSpdyStream*
EnvoyQuicClientSession::CreateIncomingStream(quic::PendingStream* /*pending*/) {
  // Disallow server initiated stream.
  NOT_REACHED_GCOVR_EXCL_LINE;
}

bool EnvoyQuicClientSession::hasDataToWrite() { return HasDataToWrite(); }

} // namespace Quic
} // namespace Envoy
