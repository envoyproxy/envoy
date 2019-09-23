#include "extensions/quic_listeners/quiche/envoy_quic_server_session.h"

#pragma GCC diagnostic push
// QUICHE allows unused parameters.
#pragma GCC diagnostic ignored "-Wunused-parameter"
// QUICHE uses offsetof().
#pragma GCC diagnostic ignored "-Winvalid-offsetof"

#include "quiche/quic/core/quic_crypto_server_stream.h"
#pragma GCC diagnostic pop

#include "common/common/assert.h"
#include "extensions/quic_listeners/quiche/envoy_quic_server_stream.h"

namespace Envoy {
namespace Quic {

EnvoyQuicServerSession::EnvoyQuicServerSession(
    const quic::QuicConfig& config, const quic::ParsedQuicVersionVector& supported_versions,
    std::unique_ptr<EnvoyQuicConnection> connection, quic::QuicSession::Visitor* visitor,
    quic::QuicCryptoServerStream::Helper* helper, const quic::QuicCryptoServerConfig* crypto_config,
    quic::QuicCompressedCertsCache* compressed_certs_cache, Event::Dispatcher& dispatcher,
    uint32_t send_buffer_limit)
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

<<<<<<< HEAD
void EnvoyQuicServerSession::addWriteFilter(Network::WriteFilterSharedPtr filter) {
  filter_manager_.addWriteFilter(filter);
}

void EnvoyQuicServerSession::addFilter(Network::FilterSharedPtr filter) {
  filter_manager_.addFilter(filter);
}

void EnvoyQuicServerSession::addReadFilter(Network::ReadFilterSharedPtr filter) {
  filter_manager_.addReadFilter(filter);
}

bool EnvoyQuicServerSession::initializeReadFilters() {
  return filter_manager_.initializeReadFilters();
}

void EnvoyQuicServerSession::addConnectionCallbacks(Network::ConnectionCallbacks& cb) {
  network_connection_callbacks_.push_back(&cb);
}

void EnvoyQuicServerSession::addBytesSentCallback(Network::Connection::BytesSentCb /*cb*/) {
  // TODO(danzh): implement to support proxy. This interface is only called from
  // TCP proxy code.
  ASSERT(false, "addBytesSentCallback is not implemented for QUIC");
}

void EnvoyQuicServerSession::enableHalfClose(bool enabled) {
  ASSERT(!enabled, "Quic connection doesn't support half close.");
}

void EnvoyQuicServerSession::setBufferLimits(uint32_t /*limit*/) {
  // Unlike TCP, write buffer limits are already set during construction.
  // Read buffer limits is not needed as connection level flow control should do
  // the work.
  ASSERT(false, "Buffer limit should be set during construction.");
}

uint32_t EnvoyQuicServerSession::bufferLimit() const {
  // As quic connection is not HTTP1.1, this method shouldn't be called by HCM.
  NOT_REACHED_GCOVR_EXCL_LINE;
}

void EnvoyQuicServerSession::close(Network::ConnectionCloseType type) {
  if (type != Network::ConnectionCloseType::NoFlush) {
    // TODO(danzh): Implement FlushWrite and FlushWriteAndDelay mode.
    ENVOY_CONN_LOG(error, "Flush write is not implemented for QUIC.", *this);
=======
void EnvoyQuicServerSession::OnCryptoHandshakeEvent(CryptoHandshakeEvent event) {
  quic::QuicServerSessionBase::OnCryptoHandshakeEvent(event);
  if (event == HANDSHAKE_CONFIRMED) {
    raiseEvent(Network::ConnectionEvent::Connected);
>>>>>>> add quic client impl
  }
}

void EnvoyQuicServerSession::adjustBytesToSend(int64_t delta) {
  bytes_to_send_ += delta;
  write_buffer_watermark_simulation_.checkHighWatermark(bytes_to_send_);
  write_buffer_watermark_simulation_.checkLowWatermark(bytes_to_send_);
}

void EnvoyQuicServerSession::onSendBufferHighWatermark() {
  ENVOY_CONN_LOG(trace, "onSendBufferHighWatermark", *this);
  for (auto callback : network_connection_callbacks_) {
    callback->onAboveWriteBufferHighWatermark();
  }
}

void EnvoyQuicServerSession::onSendBufferLowWatermark() {
  ENVOY_CONN_LOG(trace, "onSendBufferLowWatermark", *this);
  for (auto callback : network_connection_callbacks_) {
    callback->onBelowWriteBufferLowWatermark();
  }
}

} // namespace Quic
} // namespace Envoy
