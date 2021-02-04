#include "extensions/quic_listeners/quiche/envoy_quic_server_session.h"

#include <memory>

#include "common/common/assert.h"

#include "extensions/quic_listeners/quiche/envoy_quic_proof_source.h"
#include "extensions/quic_listeners/quiche/envoy_quic_server_stream.h"

namespace Envoy {
namespace Quic {

EnvoyQuicServerSession::EnvoyQuicServerSession(
    const quic::QuicConfig& config, const quic::ParsedQuicVersionVector& supported_versions,
    std::unique_ptr<EnvoyQuicConnection> connection, quic::QuicSession::Visitor* visitor,
    quic::QuicCryptoServerStream::Helper* helper, const quic::QuicCryptoServerConfig* crypto_config,
    quic::QuicCompressedCertsCache* compressed_certs_cache, Event::Dispatcher& dispatcher,
    uint32_t send_buffer_limit, Network::ListenerConfig& listener_config)
    : quic::QuicServerSessionBase(config, supported_versions, connection.get(), visitor, helper,
                                  crypto_config, compressed_certs_cache),
      QuicFilterManagerConnectionImpl(*connection, dispatcher, send_buffer_limit),
      quic_connection_(std::move(connection)), listener_config_(listener_config) {}

EnvoyQuicServerSession::~EnvoyQuicServerSession() {
  ASSERT(!quic_connection_->connected());
  QuicFilterManagerConnectionImpl::quic_connection_ = nullptr;
}

absl::string_view EnvoyQuicServerSession::requestedServerName() const {
  return {GetCryptoStream()->crypto_negotiated_params().sni};
}

std::unique_ptr<quic::QuicCryptoServerStreamBase>
EnvoyQuicServerSession::CreateQuicCryptoServerStream(
    const quic::QuicCryptoServerConfig* crypto_config,
    quic::QuicCompressedCertsCache* compressed_certs_cache) {
  return CreateCryptoServerStream(crypto_config, compressed_certs_cache, this, stream_helper());
}

quic::QuicSpdyStream* EnvoyQuicServerSession::CreateIncomingStream(quic::QuicStreamId id) {
  if (!ShouldCreateIncomingStream(id)) {
    return nullptr;
  }
  auto stream = new EnvoyQuicServerStream(id, this, quic::BIDIRECTIONAL);
  ActivateStream(absl::WrapUnique(stream));
  setUpRequestDecoder(*stream);
  if (aboveHighWatermark()) {
    stream->runHighWatermarkCallbacks();
  }
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
  maybeCreateNetworkFilters();
  // This is only reached once, when handshake is done.
  raiseConnectionEvent(Network::ConnectionEvent::Connected);
}

bool EnvoyQuicServerSession::hasDataToWrite() { return HasDataToWrite(); }

void EnvoyQuicServerSession::OnTlsHandshakeComplete() {
  quic::QuicServerSessionBase::OnTlsHandshakeComplete();
  maybeCreateNetworkFilters();
  raiseConnectionEvent(Network::ConnectionEvent::Connected);
}

void EnvoyQuicServerSession::maybeCreateNetworkFilters() {
  auto proof_source_details =
      dynamic_cast<const EnvoyQuicProofSourceDetails*>(GetCryptoStream()->ProofSourceDetails());
  ASSERT(proof_source_details != nullptr,
         "ProofSource didn't provide ProofSource::Details. No filter chain will be installed.");

  const bool has_filter_initialized =
      listener_config_.filterChainFactory().createNetworkFilterChain(
          *this, proof_source_details->filterChain().networkFilterFactories());
  ASSERT(has_filter_initialized);
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

} // namespace Quic
} // namespace Envoy
