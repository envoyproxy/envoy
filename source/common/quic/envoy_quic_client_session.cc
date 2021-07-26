#include "source/common/quic/envoy_quic_client_session.h"

#include "source/common/quic/envoy_quic_utils.h"

namespace Envoy {
namespace Quic {

EnvoyQuicClientSession::EnvoyQuicClientSession(
    const quic::QuicConfig& config, const quic::ParsedQuicVersionVector& supported_versions,
    std::unique_ptr<EnvoyQuicClientConnection> connection, const quic::QuicServerId& server_id,
    std::shared_ptr<quic::QuicCryptoClientConfig> crypto_config,
    quic::QuicClientPushPromiseIndex* push_promise_index, Event::Dispatcher& dispatcher,
    uint32_t send_buffer_limit, EnvoyQuicCryptoClientStreamFactoryInterface& crypto_stream_factory,
    QuicStatNames& quic_stat_names, Stats::Scope& scope)
    : QuicFilterManagerConnectionImpl(*connection, connection->connection_id(), dispatcher,
                                      send_buffer_limit),
      quic::QuicSpdyClientSession(config, supported_versions, connection.release(), server_id,
                                  crypto_config.get(), push_promise_index),
      host_name_(server_id.host()), crypto_config_(crypto_config),
      crypto_stream_factory_(crypto_stream_factory), quic_stat_names_(quic_stat_names),
      scope_(scope) {}

EnvoyQuicClientSession::~EnvoyQuicClientSession() {
  ASSERT(!connection()->connected());
  network_connection_ = nullptr;
}

absl::string_view EnvoyQuicClientSession::requestedServerName() const { return host_name_; }

void EnvoyQuicClientSession::connect() {
  dynamic_cast<EnvoyQuicClientConnection*>(network_connection_)->setUpConnectionSocket(*this);
  // Start version negotiation and crypto handshake during which the connection may fail if server
  // doesn't support the one and only supported version.
  CryptoConnect();
}

void EnvoyQuicClientSession::OnConnectionClosed(const quic::QuicConnectionCloseFrame& frame,
                                                quic::ConnectionCloseSource source) {
  quic::QuicSpdyClientSession::OnConnectionClosed(frame, source);
  quic_stat_names_.chargeQuicConnectionCloseStats(scope_, frame.quic_error_code, source, true);
  onConnectionCloseEvent(frame, source, version());
}

void EnvoyQuicClientSession::Initialize() {
  quic::QuicSpdyClientSession::Initialize();
  initialized_ = true;
  network_connection_->setEnvoyConnection(*this);
}

void EnvoyQuicClientSession::OnCanWrite() {
  if (quic::VersionUsesHttp3(transport_version())) {
    quic::QuicSpdyClientSession::OnCanWrite();
  } else {
    // This will cause header stream flushing. It is the only place to discount bytes buffered in
    // header stream from connection watermark buffer during writing.
    SendBufferMonitor::ScopedWatermarkBufferUpdater updater(headers_stream(), this);
    quic::QuicSpdyClientSession::OnCanWrite();
  }
  maybeApplyDelayClosePolicy();
}

void EnvoyQuicClientSession::OnGoAway(const quic::QuicGoAwayFrame& frame) {
  ENVOY_CONN_LOG(debug, "GOAWAY received with error {}: {}", *this,
                 quic::QuicErrorCodeToString(frame.error_code), frame.reason_phrase);
  quic::QuicSpdyClientSession::OnGoAway(frame);
  if (http_connection_callbacks_ != nullptr) {
    http_connection_callbacks_->onGoAway(quicErrorCodeToEnvoyErrorCode(frame.error_code));
  }
}

void EnvoyQuicClientSession::OnHttp3GoAway(uint64_t stream_id) {
  ENVOY_CONN_LOG(debug, "HTTP/3 GOAWAY received", *this);
  quic::QuicSpdyClientSession::OnHttp3GoAway(stream_id);
  if (http_connection_callbacks_ != nullptr) {
    // HTTP/3 GOAWAY doesn't have an error code field.
    http_connection_callbacks_->onGoAway(Http::GoAwayErrorCode::NoError);
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
  ASSERT(codec_stats_.has_value() && http3_options_.has_value());
  return std::make_unique<EnvoyQuicClientStream>(GetNextOutgoingBidirectionalStreamId(), this,
                                                 quic::BIDIRECTIONAL, codec_stats_.value(),
                                                 http3_options_.value());
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

const quic::QuicConnection* EnvoyQuicClientSession::quicConnection() const {
  return initialized_ ? connection() : nullptr;
}

quic::QuicConnection* EnvoyQuicClientSession::quicConnection() {
  return initialized_ ? connection() : nullptr;
}

void EnvoyQuicClientSession::OnTlsHandshakeComplete() {
  raiseConnectionEvent(Network::ConnectionEvent::Connected);
}

size_t EnvoyQuicClientSession::WriteHeadersOnHeadersStream(
    quic::QuicStreamId id, spdy::SpdyHeaderBlock headers, bool fin,
    const spdy::SpdyStreamPrecedence& precedence,
    quic::QuicReferenceCountedPointer<quic::QuicAckListenerInterface> ack_listener) {
  ASSERT(!quic::VersionUsesHttp3(transport_version()));
  // gQUIC headers are sent on a dedicated stream. Only count the bytes sent against
  // connection level watermark buffer. Do not count them into stream level
  // watermark buffer, because it is impossible to identify which byte belongs
  // to which stream when the buffered bytes are drained in headers stream.
  // This updater may be in the scope of another one in OnCanWrite(), in such
  // case, this one doesn't update the watermark.
  SendBufferMonitor::ScopedWatermarkBufferUpdater updater(headers_stream(), this);
  return quic::QuicSpdyClientSession::WriteHeadersOnHeadersStream(id, std::move(headers), fin,
                                                                  precedence, ack_listener);
}

std::unique_ptr<quic::QuicCryptoClientStreamBase> EnvoyQuicClientSession::CreateQuicCryptoStream() {
  return crypto_stream_factory_.createEnvoyQuicCryptoClientStream(
      server_id(), this, crypto_config()->proof_verifier()->CreateDefaultContext(), crypto_config(),
      this, /*has_application_state = */ version().UsesHttp3());
}

} // namespace Quic
} // namespace Envoy
