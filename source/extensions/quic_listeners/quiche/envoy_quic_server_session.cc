#include "extensions/quic_listeners/quiche/envoy_quic_server_session.h"

#pragma GCC diagnostic push
// QUICHE allows unused parameters.
#pragma GCC diagnostic ignored "-Wunused-parameter"
// QUICHE uses offsetof().
#pragma GCC diagnostic ignored "-Winvalid-offsetof"

#include "quiche/quic/core/quic_crypto_server_stream.h"
#pragma GCC diagnostic pop

#include "extensions/quic_listeners/quiche/envoy_quic_server_stream.h"

namespace Envoy {
namespace Quic {

EnvoyQuicServerSession::EnvoyQuicServerSession(
    const quic::QuicConfig& config, const quic::ParsedQuicVersionVector& supported_versions,
    std::unique_ptr<EnvoyQuicConnection> connection, quic::QuicSession::Visitor* visitor,
    quic::QuicCryptoServerStream::Helper* helper, const quic::QuicCryptoServerConfig* crypto_config,
    quic::QuicCompressedCertsCache* compressed_certs_cache, Event::Dispatcher& dispatcher)
    : quic::QuicServerSessionBase(config, supported_versions, connection.get(), visitor, helper,
                                  crypto_config, compressed_certs_cache),
      QuicFilterManagerConnectionImpl(std::move(connection), dispatcher) {}

absl::string_view EnvoyQuicServerSession::requestedServerName() const {
  return {GetCryptoStream()->crypto_negotiated_params().sni};
}

quic::QuicCryptoServerStreamBase* EnvoyQuicServerSession::CreateQuicCryptoServerStream(
    const quic::QuicCryptoServerConfig* crypto_config,
    quic::QuicCompressedCertsCache* compressed_certs_cache) {
  return new quic::QuicCryptoServerStream(crypto_config, compressed_certs_cache, this,
                                          stream_helper());
}

quic::QuicSpdyStream* EnvoyQuicServerSession::CreateIncomingStream(quic::QuicStreamId id) {
  if (!ShouldCreateIncomingStream(id)) {
    return nullptr;
  }
  auto stream = new EnvoyQuicServerStream(id, this, quic::BIDIRECTIONAL);
  ActivateStream(absl::WrapUnique(stream));
  setUpRequestDecoder(*stream);
  return stream;
}

quic::QuicSpdyStream*
EnvoyQuicServerSession::CreateIncomingStream(quic::PendingStream* /*pending*/) {
  // Only client side server push stream should trigger this call.
  NOT_REACHED_GCOVR_EXCL_LINE;
}

quic::QuicSpdyStream* EnvoyQuicServerSession::CreateOutgoingBidirectionalStream() {
  // Disallow server initiated stream.
  NOT_REACHED_GCOVR_EXCL_LINE;
}

quic::QuicSpdyStream* EnvoyQuicServerSession::CreateOutgoingUnidirectionalStream() {
  NOT_REACHED_GCOVR_EXCL_LINE;
}

void EnvoyQuicServerSession::setUpRequestDecoder(EnvoyQuicStream& stream) {
  ASSERT(http_connection_callbacks_ != nullptr);
  Http::StreamDecoder& decoder = http_connection_callbacks_->newStream(stream);
  stream.setDecoder(decoder);
}

void EnvoyQuicServerSession::OnConnectionClosed(const quic::QuicConnectionCloseFrame& frame,
                                                quic::ConnectionCloseSource source) {
  quic::QuicServerSessionBase::OnConnectionClosed(frame, source);
  onConnectionCloseEvent(frame, source);
}

void EnvoyQuicServerSession::Initialize() {
  quic::QuicServerSessionBase::Initialize();
  quic_connection_->setEnvoyConnection(*this);
}

void EnvoyQuicServerSession::SendGoAway(quic::QuicErrorCode error_code, const std::string& reason) {
  if (transport_version() < quic::QUIC_VERSION_99) {
    quic::QuicServerSessionBase::SendGoAway(error_code, reason);
  }
}

void EnvoyQuicServerSession::OnCryptoHandshakeEvent(CryptoHandshakeEvent event) {
  quic::QuicServerSessionBase::OnCryptoHandshakeEvent(event);
  if (event == HANDSHAKE_CONFIRMED) {
    raiseEvent(Network::ConnectionEvent::Connected);
  }
}

} // namespace Quic
} // namespace Envoy
