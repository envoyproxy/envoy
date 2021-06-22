#include "source/common/quic/envoy_quic_server_session.h"

#include <memory>

#include "source/common/common/assert.h"
#include "source/common/quic/envoy_quic_proof_source.h"
#include "source/common/quic/envoy_quic_server_stream.h"

namespace Envoy {
namespace Quic {

EnvoyQuicServerSession::EnvoyQuicServerSession(
    const quic::QuicConfig& config, const quic::ParsedQuicVersionVector& supported_versions,
    std::unique_ptr<EnvoyQuicServerConnection> connection, quic::QuicSession::Visitor* visitor,
    quic::QuicCryptoServerStream::Helper* helper, const quic::QuicCryptoServerConfig* crypto_config,
    quic::QuicCompressedCertsCache* compressed_certs_cache, Event::Dispatcher& dispatcher,
    uint32_t send_buffer_limit, QuicStatNames& quic_stat_names, Stats::Scope& listener_scope,
    EnvoyQuicCryptoServerStreamFactoryInterface& crypto_server_stream_factory,
    OptRef<const Network::TransportSocketFactory> transport_socket_factory)
    : quic::QuicServerSessionBase(config, supported_versions, connection.get(), visitor, helper,
                                  crypto_config, compressed_certs_cache),
      QuicFilterManagerConnectionImpl(*connection, connection->connection_id(), dispatcher,
                                      send_buffer_limit),
      quic_connection_(std::move(connection)), quic_stat_names_(quic_stat_names),
      listener_scope_(listener_scope), crypto_server_stream_factory_(crypto_server_stream_factory),
      transport_socket_factory_(transport_socket_factory) {}

EnvoyQuicServerSession::~EnvoyQuicServerSession() {
  ASSERT(!quic_connection_->connected());
  QuicFilterManagerConnectionImpl::network_connection_ = nullptr;
}

absl::string_view EnvoyQuicServerSession::requestedServerName() const {
  return {GetCryptoStream()->crypto_negotiated_params().sni};
}

std::unique_ptr<quic::QuicCryptoServerStreamBase>
EnvoyQuicServerSession::CreateQuicCryptoServerStream(
    const quic::QuicCryptoServerConfig* crypto_config,
    quic::QuicCompressedCertsCache* compressed_certs_cache) {
  return crypto_server_stream_factory_.createEnvoyQuicCryptoServerStream(
      crypto_config, compressed_certs_cache, this, stream_helper(), transport_socket_factory_,
      dispatcher());
}

quic::QuicSpdyStream* EnvoyQuicServerSession::CreateIncomingStream(quic::QuicStreamId id) {
  if (!ShouldCreateIncomingStream(id)) {
    return nullptr;
  }
  if (!codec_stats_.has_value() || !http3_options_.has_value()) {
    ENVOY_BUG(false,
              fmt::format(
                  "Quic session {} attempts to create stream {} before HCM filter is initialized.",
                  this->id(), id));
    return nullptr;
  }
  auto stream = new EnvoyQuicServerStream(id, this, quic::BIDIRECTIONAL, codec_stats_.value(),
                                          http3_options_.value(), headers_with_underscores_action_);
  ActivateStream(absl::WrapUnique(stream));
  if (aboveHighWatermark()) {
    stream->runHighWatermarkCallbacks();
  }
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

void EnvoyQuicServerSession::setUpRequestDecoder(EnvoyQuicServerStream& stream) {
  ASSERT(http_connection_callbacks_ != nullptr);
  Http::RequestDecoder& decoder = http_connection_callbacks_->newStream(stream);
  stream.setRequestDecoder(decoder);
}

void EnvoyQuicServerSession::OnConnectionClosed(const quic::QuicConnectionCloseFrame& frame,
                                                quic::ConnectionCloseSource source) {
  quic::QuicServerSessionBase::OnConnectionClosed(frame, source);
  onConnectionCloseEvent(frame, source);
}

void EnvoyQuicServerSession::Initialize() {
  quic::QuicServerSessionBase::Initialize();
  initialized_ = true;
  quic_connection_->setEnvoyConnection(*this);
}

void EnvoyQuicServerSession::OnCanWrite() {
  if (quic::VersionUsesHttp3(transport_version())) {
    quic::QuicServerSessionBase::OnCanWrite();
  } else {
    SendBufferMonitor::ScopedWatermarkBufferUpdater updater(headers_stream(), this);
    quic::QuicServerSessionBase::OnCanWrite();
  }
  // Do not update delay close state according to connection level packet egress because that is
  // equivalent to TCP transport layer egress. But only do so if the session gets chance to write.
  maybeApplyDelayClosePolicy();
}

void EnvoyQuicServerSession::SetDefaultEncryptionLevel(quic::EncryptionLevel level) {
  quic::QuicServerSessionBase::SetDefaultEncryptionLevel(level);
  if (level != quic::ENCRYPTION_FORWARD_SECURE) {
    return;
  }
  // This is only reached once, when handshake is done.
  raiseConnectionEvent(Network::ConnectionEvent::Connected);
}

bool EnvoyQuicServerSession::hasDataToWrite() { return HasDataToWrite(); }

const quic::QuicConnection* EnvoyQuicServerSession::quicConnection() const {
  return initialized_ ? connection() : nullptr;
}

quic::QuicConnection* EnvoyQuicServerSession::quicConnection() {
  return initialized_ ? connection() : nullptr;
}

void EnvoyQuicServerSession::OnTlsHandshakeComplete() {
  quic::QuicServerSessionBase::OnTlsHandshakeComplete();
  raiseConnectionEvent(Network::ConnectionEvent::Connected);
}

size_t EnvoyQuicServerSession::WriteHeadersOnHeadersStream(
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
  return quic::QuicServerSessionBase::WriteHeadersOnHeadersStream(id, std::move(headers), fin,
                                                                  precedence, ack_listener);
}

void EnvoyQuicServerSession::MaybeSendRstStreamFrame(quic::QuicStreamId id,
                                                     quic::QuicRstStreamErrorCode error,
                                                     quic::QuicStreamOffset bytes_written) {
  QuicServerSessionBase::MaybeSendRstStreamFrame(id, error, bytes_written);
  quic_stat_names_.chargeQuicResetStreamErrorStats(listener_scope_, error, /*from_self*/ true,
                                                   /*is_upstream*/ false);
}

void EnvoyQuicServerSession::OnRstStream(const quic::QuicRstStreamFrame& frame) {
  QuicServerSessionBase::OnRstStream(frame);
  quic_stat_names_.chargeQuicResetStreamErrorStats(listener_scope_, frame.error_code,
                                                   /*from_self*/ false, /*is_upstream*/ false);
}

} // namespace Quic
} // namespace Envoy
