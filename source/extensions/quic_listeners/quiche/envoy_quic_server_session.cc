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
      quic_connection_(std::move(connection)), filter_manager_(*this), dispatcher_(dispatcher),
      stream_info_(dispatcher.timeSource()) {
  // TODO(danzh): Use QUIC specific enum value.
  stream_info_.protocol(Http::Protocol::Http2);
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
  for (auto callback : network_connection_callbacks_) {
    // Tell filters about connection close.
    callback->onEvent(source == quic::ConnectionCloseSource::FROM_PEER
                          ? Network::ConnectionEvent::RemoteClose
                          : Network::ConnectionEvent::LocalClose);
  }
  transport_failure_reason_ = absl::StrCat(quic::QuicErrorCodeToString(frame.quic_error_code),
                                           " with details: ", frame.error_details);
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
  // TODO(danzh): add interface to quic for connection level buffer throttling.
  // Currently read buffer is capped by connection level flow control. And
  // write buffer is not capped.
  ENVOY_CONN_LOG(error, "Quic manages its own buffer currently.", *this);
}

uint32_t EnvoyQuicServerSession::bufferLimit() const {
  // As quic connection is not HTTP1.1, this method shouldn't be called by HCM.
  NOT_REACHED_GCOVR_EXCL_LINE;
}

void EnvoyQuicServerSession::close(Network::ConnectionCloseType type) {
  if (type != Network::ConnectionCloseType::NoFlush) {
    // TODO(danzh): Implement FlushWrite and FlushWriteAndDelay mode.
    ENVOY_CONN_LOG(error, "Flush write is not implemented for QUIC.", *this);
  }
  connection()->CloseConnection(quic::QUIC_NO_ERROR, "Closed by application",
                                quic::ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET);
}

void EnvoyQuicServerSession::setDelayedCloseTimeout(std::chrono::milliseconds timeout) {
  ASSERT(timeout == std::chrono::milliseconds::zero(),
         "Delayed close of connection is not supported");
}

std::chrono::milliseconds EnvoyQuicServerSession::delayedCloseTimeout() const {
  // Not called outside of Network::ConnectionImpl. Maybe remove this interface
  // from Network::Connection.
  NOT_REACHED_GCOVR_EXCL_LINE;
}

const Network::ConnectionSocket::OptionsSharedPtr& EnvoyQuicServerSession::socketOptions() const {
  ENVOY_CONN_LOG(
      error,
      "QUIC connection socket is merely a wrapper, and doesn't have any specific socket options.",
      *this);
  return quic_connection_->connectionSocket()->options();
}

absl::string_view EnvoyQuicServerSession::requestedServerName() const {
  return {GetCryptoStream()->crypto_negotiated_params().sni};
}

const Network::Address::InstanceConstSharedPtr& EnvoyQuicServerSession::remoteAddress() const {
  ASSERT(quic_connection_->connectionSocket() != nullptr,
         "remoteAddress() should only be called after OnPacketHeader");
  return quic_connection_->connectionSocket()->remoteAddress();
}

const Network::Address::InstanceConstSharedPtr& EnvoyQuicServerSession::localAddress() const {
  ASSERT(quic_connection_->connectionSocket() != nullptr,
         "localAddress() should only be called after OnPacketHeader");
  return quic_connection_->connectionSocket()->localAddress();
}

Ssl::ConnectionInfoConstSharedPtr EnvoyQuicServerSession::ssl() const {
  // TODO(danzh): construct Ssl::ConnectionInfo from crypto stream
  ENVOY_CONN_LOG(error, "Ssl::ConnectionInfo instance is not populated.", *this);
  return nullptr;
}

void EnvoyQuicServerSession::rawWrite(Buffer::Instance& /*data*/, bool /*end_stream*/) {
  // Network filter should stop iteration.
  NOT_REACHED_GCOVR_EXCL_LINE;
}

} // namespace Quic
} // namespace Envoy
