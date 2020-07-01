#include "extensions/quic_listeners/quiche/envoy_quic_server_session.h"

#include <memory>

#include "common/common/assert.h"

#include "extensions/quic_listeners/quiche/envoy_quic_crypto_server_stream.h"
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
  switch (connection()->version().handshake_protocol) {
  case quic::PROTOCOL_QUIC_CRYPTO:
    return std::make_unique<EnvoyQuicCryptoServerStream>(crypto_config, compressed_certs_cache,
                                                         this, stream_helper());
  case quic::PROTOCOL_TLS1_3:
    return std::make_unique<EnvoyQuicTlsServerHandshaker>(this, *crypto_config);
  case quic::PROTOCOL_UNSUPPORTED:
    break;
  }
  QUIC_BUG << "Unknown handshake protocol: "
           << static_cast<int>(connection()->version().handshake_protocol);
  return nullptr;
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
  quic::QuicServerSessionBase::OnCanWrite();
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

  const DetailsWithFilterChain* proof_source_detail =
      dynamic_cast<const EnvoyQuicCryptoServerStream*>(GetCryptoStream())->proofSourceDetails();
  if (proof_source_detail == nullptr) {
    // Unit tests using TestProofSource might not set ProofSource::Details.
    std::cerr << "========= No proof source details\n";
    return;
  }
  createNetworkFilters(proof_source_detail->filterChain());
}

void EnvoyQuicServerSession::createNetworkFilters(const Network::FilterChain& filter_chain) {
  const bool empty_filter_chain = !listener_config_.filterChainFactory().createNetworkFilterChain(
      *this, filter_chain.networkFilterFactories());
  if (empty_filter_chain) {
    // TODO(danzh) check empty filter chain at config load time instead of here.
    connection()->CloseConnection(quic::QUIC_CRYPTO_INTERNAL_ERROR,
                                  "closing connection: filter chain is empty",
                                  quic::ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET);
  }
}

bool EnvoyQuicServerSession::hasDataToWrite() { return HasDataToWrite(); }

void EnvoyQuicServerSession::OnOneRttKeysAvailable() {
  quic::QuicServerSessionBase::OnOneRttKeysAvailable();
  raiseConnectionEvent(Network::ConnectionEvent::Connected);

  const DetailsWithFilterChain* details =
      dynamic_cast<const EnvoyQuicTlsServerHandshaker*>(GetCryptoStream())->proofSourceDetails();
  if (details == nullptr) {
    std::cerr << "========= No proof source details\n";

    return;
  }
  createNetworkFilters(details->filterChain());
}

} // namespace Quic
} // namespace Envoy
