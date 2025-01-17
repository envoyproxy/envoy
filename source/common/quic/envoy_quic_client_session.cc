#include "source/common/quic/envoy_quic_client_session.h"

#include <openssl/ssl.h>

#include <memory>

#include "envoy/network/transport_socket.h"

#include "source/common/event/dispatcher_impl.h"
#include "source/common/quic/envoy_quic_proof_verifier.h"
#include "source/common/quic/envoy_quic_utils.h"
#include "source/common/quic/quic_filter_manager_connection_impl.h"

namespace Envoy {
namespace Quic {

// This class is only used to provide extra context during certificate validation. As such it does
// not implement the various interfaces which are irrelevant to certificate validation.
class CertValidationContext : public Network::TransportSocketCallbacks {
public:
  CertValidationContext(EnvoyQuicClientSession& session, Network::IoHandle& io_handle)
      : session_(session), io_handle_(io_handle) {}

  // Network::TransportSocketCallbacks
  Network::IoHandle& ioHandle() final { return io_handle_; }
  const Network::IoHandle& ioHandle() const override { return io_handle_; }
  Network::Connection& connection() override { return session_; }
  // Below methods shouldn't be called during cert verification.
  void raiseEvent(Network::ConnectionEvent /*event*/) override { PANIC("unexpectedly reached"); }
  bool shouldDrainReadBuffer() override { PANIC("unexpectedly reached"); }
  void setTransportSocketIsReadable() override { PANIC("unexpectedly reached"); }
  void flushWriteBuffer() override { PANIC("unexpectedly reached"); }

private:
  EnvoyQuicClientSession& session_;
  Network::IoHandle& io_handle_;
};

using CertValidationContextPtr = std::unique_ptr<CertValidationContext>;

// An implementation of the verify context interface.
class EnvoyQuicProofVerifyContextImpl : public EnvoyQuicProofVerifyContext {
public:
  EnvoyQuicProofVerifyContextImpl(
      Event::Dispatcher& dispatcher, const bool is_server,
      const Network::TransportSocketOptionsConstSharedPtr& transport_socket_options,
      QuicSslConnectionInfo& ssl_info, CertValidationContextPtr validation_context)
      : dispatcher_(dispatcher), is_server_(is_server),
        transport_socket_options_(transport_socket_options), ssl_info_(ssl_info),
        validation_context_(std::move(validation_context)) {}

  // EnvoyQuicProofVerifyContext
  bool isServer() const override { return is_server_; }
  Event::Dispatcher& dispatcher() const override { return dispatcher_; }
  const Network::TransportSocketOptionsConstSharedPtr& transportSocketOptions() const override {
    return transport_socket_options_;
  }

  Extensions::TransportSockets::Tls::CertValidator::ExtraValidationContext
  extraValidationContext() const override {
    ASSERT(ssl_info_.ssl());
    return {validation_context_.get()};
  }

private:
  Event::Dispatcher& dispatcher_;
  const bool is_server_;
  const Network::TransportSocketOptionsConstSharedPtr& transport_socket_options_;
  QuicSslConnectionInfo& ssl_info_;
  CertValidationContextPtr validation_context_;
};

EnvoyQuicClientSession::EnvoyQuicClientSession(
    const quic::QuicConfig& config, const quic::ParsedQuicVersionVector& supported_versions,
    std::unique_ptr<EnvoyQuicClientConnection> connection, const quic::QuicServerId& server_id,
    std::shared_ptr<quic::QuicCryptoClientConfig> crypto_config, Event::Dispatcher& dispatcher,
    uint32_t send_buffer_limit, EnvoyQuicCryptoClientStreamFactoryInterface& crypto_stream_factory,
    QuicStatNames& quic_stat_names, OptRef<Http::HttpServerPropertiesCache> rtt_cache,
    Stats::Scope& scope,
    const Network::TransportSocketOptionsConstSharedPtr& transport_socket_options,
    OptRef<Network::UpstreamTransportSocketFactory> transport_socket_factory)
    : QuicFilterManagerConnectionImpl(
          *connection, connection->connection_id(), dispatcher, send_buffer_limit,
          std::make_shared<QuicSslConnectionInfo>(*this),
          std::make_unique<StreamInfo::StreamInfoImpl>(
              dispatcher.timeSource(),
              connection->connectionSocket()->connectionInfoProviderSharedPtr(),
              StreamInfo::FilterState::LifeSpan::Connection),
          quic_stat_names, scope),
      quic::QuicSpdyClientSession(config, supported_versions, connection.release(), server_id,
                                  crypto_config.get()),
      crypto_config_(crypto_config), crypto_stream_factory_(crypto_stream_factory),
      rtt_cache_(rtt_cache), transport_socket_options_(transport_socket_options),
      transport_socket_factory_(makeOptRefFromPtr(
          dynamic_cast<QuicTransportSocketFactoryBase*>(transport_socket_factory.ptr()))) {
  streamInfo().setUpstreamInfo(std::make_shared<StreamInfo::UpstreamInfoImpl>());
  if (transport_socket_options_ != nullptr &&
      !transport_socket_options_->applicationProtocolListOverride().empty()) {
    configured_alpns_ = transport_socket_options_->applicationProtocolListOverride();
  } else if (transport_socket_factory_.has_value() &&
             !transport_socket_factory_->supportedAlpnProtocols().empty()) {
    configured_alpns_ =
        std::vector<std::string>(transport_socket_factory_->supportedAlpnProtocols().begin(),
                                 transport_socket_factory_->supportedAlpnProtocols().end());
  }
#ifdef ENVOY_ENABLE_HTTP_DATAGRAMS
  http_datagram_support_ = quic::HttpDatagramSupport::kRfc;
#endif
}

EnvoyQuicClientSession::~EnvoyQuicClientSession() {
  ASSERT(!connection()->connected());
  network_connection_ = nullptr;
  if (registry_.has_value()) {
    registry_->unregisterObserver(*network_connectivity_observer_);
  }
}

absl::string_view EnvoyQuicClientSession::requestedServerName() const { return server_id().host(); }

void EnvoyQuicClientSession::connect() {
  streamInfo().upstreamInfo()->upstreamTiming().onUpstreamConnectStart(dispatcher_.timeSource());
  dynamic_cast<EnvoyQuicClientConnection*>(network_connection_)
      ->setUpConnectionSocket(
          *static_cast<EnvoyQuicClientConnection*>(connection())->connectionSocket(), *this);
  // Start version negotiation and crypto handshake during which the connection may fail if server
  // doesn't support the one and only supported version.
  CryptoConnect();
}

void EnvoyQuicClientSession::OnConnectionClosed(const quic::QuicConnectionCloseFrame& frame,
                                                quic::ConnectionCloseSource source) {
  // Latch latest srtt.
  if (OneRttKeysAvailable() && rtt_cache_) {
    const quic::QuicConnectionStats& stats = connection()->GetStats();
    if (stats.srtt_us > 0) {
      Http::HttpServerPropertiesCache::Origin origin("https", server_id().host(),
                                                     server_id().port());
      rtt_cache_->setSrtt(origin, std::chrono::microseconds(stats.srtt_us));
    }
  }
  quic::QuicSpdyClientSession::OnConnectionClosed(frame, source);
  quic_stat_names_.chargeQuicConnectionCloseStats(stats_scope_, frame.quic_error_code, source,
                                                  true);
  onConnectionCloseEvent(frame, source, version());
}

void EnvoyQuicClientSession::Initialize() {
  quic::QuicSpdyClientSession::Initialize();
  initialized_ = true;
  network_connection_->setEnvoyConnection(*this, *this);
}

void EnvoyQuicClientSession::OnCanWrite() {
  uint64_t old_bytes_to_send = bytesToSend();
  quic::QuicSpdyClientSession::OnCanWrite();
  const bool has_sent_any_data = bytesToSend() != old_bytes_to_send;
  maybeUpdateDelayCloseTimer(has_sent_any_data);
}

void EnvoyQuicClientSession::OnHttp3GoAway(uint64_t stream_id) {
  ENVOY_CONN_LOG(debug, "HTTP/3 GOAWAY received", *this);
  quic::QuicSpdyClientSession::OnHttp3GoAway(stream_id);
  if (http_connection_callbacks_ != nullptr) {
    // HTTP/3 GOAWAY doesn't have an error code field.
    http_connection_callbacks_->onGoAway(Http::GoAwayErrorCode::NoError);
  }
}

void EnvoyQuicClientSession::OnRstStream(const quic::QuicRstStreamFrame& frame) {
  QuicSpdyClientSession::OnRstStream(frame);
  incrementSentQuicResetStreamErrorStats(frame.error(),
                                         /*from_self*/ false, /*is_upstream*/ true);
}

void EnvoyQuicClientSession::OnCanCreateNewOutgoingStream(bool unidirectional) {
  if (!http_connection_callbacks_ || unidirectional) {
    return;
  }
  uint32_t streams_available = streamsAvailable();
  http_connection_callbacks_->onMaxStreamsChanged(streams_available);
}

std::unique_ptr<quic::QuicSpdyClientStream> EnvoyQuicClientSession::CreateClientStream() {
  ASSERT(codec_stats_.has_value() && http3_options_.has_value());
  return std::make_unique<EnvoyQuicClientStream>(GetNextOutgoingBidirectionalStreamId(), this,
                                                 quic::BIDIRECTIONAL, codec_stats_.value(),
                                                 http3_options_.value());
}

quic::QuicSpdyStream* EnvoyQuicClientSession::CreateIncomingStream(quic::QuicStreamId /*id*/) {
  // Envoy doesn't support server initiated stream.
  return nullptr;
}

quic::QuicSpdyStream*
EnvoyQuicClientSession::CreateIncomingStream(quic::PendingStream* /*pending*/) {
  // Envoy doesn't support server push.
  IS_ENVOY_BUG("unexpectes server push call");
  return nullptr;
}

bool EnvoyQuicClientSession::hasDataToWrite() { return HasDataToWrite(); }

const quic::QuicConnection* EnvoyQuicClientSession::quicConnection() const {
  return initialized_ ? connection() : nullptr;
}

quic::QuicConnection* EnvoyQuicClientSession::quicConnection() {
  return initialized_ ? connection() : nullptr;
}

uint64_t EnvoyQuicClientSession::streamsAvailable() {
  const quic::UberQuicStreamIdManager& manager = ietf_streamid_manager();
  ASSERT(manager.max_outgoing_bidirectional_streams() >=
         manager.outgoing_bidirectional_stream_count());
  uint32_t streams_available =
      manager.max_outgoing_bidirectional_streams() - manager.outgoing_bidirectional_stream_count();
  return streams_available;
}

void EnvoyQuicClientSession::OnTlsHandshakeComplete() {
  quic::QuicSpdyClientSession::OnTlsHandshakeComplete();

  // Fake this to make sure we set the connection pool stream limit correctly
  // before use. This may result in OnCanCreateNewOutgoingStream with zero
  // available streams.
  OnCanCreateNewOutgoingStream(false);
  streamInfo().upstreamInfo()->upstreamTiming().onUpstreamConnectComplete(dispatcher_.timeSource());
  streamInfo().upstreamInfo()->upstreamTiming().onUpstreamHandshakeComplete(
      dispatcher_.timeSource());

  raiseConnectionEvent(Network::ConnectionEvent::Connected);
}

std::unique_ptr<quic::QuicCryptoClientStreamBase> EnvoyQuicClientSession::CreateQuicCryptoStream() {
  return crypto_stream_factory_.createEnvoyQuicCryptoClientStream(
      server_id(), this,
      std::make_unique<EnvoyQuicProofVerifyContextImpl>(
          dispatcher_, /*is_server=*/false, transport_socket_options_, *quic_ssl_info_,
          std::make_unique<CertValidationContext>(
              *this, network_connection_->connectionSocket()->ioHandle())),
      crypto_config(), this, /*has_application_state = */ version().UsesHttp3());
}

void EnvoyQuicClientSession::setHttp3Options(
    const envoy::config::core::v3::Http3ProtocolOptions& http3_options) {
  QuicFilterManagerConnectionImpl::setHttp3Options(http3_options);
  if (!http3_options_->has_quic_protocol_options()) {
    return;
  }
  static_cast<EnvoyQuicClientConnection*>(connection())
      ->setNumPtosForPortMigration(PROTOBUF_GET_WRAPPED_OR_DEFAULT(
          http3_options.quic_protocol_options(), num_timeouts_to_trigger_port_migration, 0));

  if (http3_options_->quic_protocol_options().has_connection_keepalive()) {
    const uint64_t initial_interval = PROTOBUF_GET_MS_OR_DEFAULT(
        http3_options_->quic_protocol_options().connection_keepalive(), initial_interval, 0);
    const uint64_t max_interval =
        PROTOBUF_GET_MS_OR_DEFAULT(http3_options_->quic_protocol_options().connection_keepalive(),
                                   max_interval, quic::kPingTimeoutSecs);
    // If the keepalive max_interval is configured to zero, disable the probe completely.
    if (max_interval == 0u) {
      disable_keepalive_ = true;
      return;
    }
    connection()->set_keep_alive_ping_timeout(
        quic::QuicTime::Delta::FromMilliseconds(max_interval));
    if (max_interval > initial_interval && initial_interval > 0u) {
      connection()->set_initial_retransmittable_on_wire_timeout(
          quic::QuicTime::Delta::FromMilliseconds(initial_interval));
    }
  }
}

bool EnvoyQuicClientSession::ShouldKeepConnectionAlive() const {
  // Do not probe at all if keepalive is disabled via config.
  return !disable_keepalive_ && quic::QuicSpdyClientSession::ShouldKeepConnectionAlive();
}

void EnvoyQuicClientSession::OnProofVerifyDetailsAvailable(
    const quic::ProofVerifyDetails& verify_details) {
  if (static_cast<const CertVerifyResult&>(verify_details).isValid()) {
    quic_ssl_info_->onCertValidated();
  }
}

void EnvoyQuicClientSession::OnNewEncryptionKeyAvailable(
    quic::EncryptionLevel level, std::unique_ptr<quic::QuicEncrypter> encrypter) {
  quic::QuicSpdyClientSession::OnNewEncryptionKeyAvailable(level, std::move(encrypter));
  if (level == quic::ENCRYPTION_ZERO_RTT) {
    ENVOY_CONN_LOG(trace, "able to send early data", *this);
    raiseConnectionEvent(Network::ConnectionEvent::ConnectedZeroRtt);
  }
}
void EnvoyQuicClientSession::OnServerPreferredAddressAvailable(
    const quic::QuicSocketAddress& server_preferred_address) {
  static_cast<EnvoyQuicClientConnection*>(connection())
      ->probeAndMigrateToServerPreferredAddress(server_preferred_address);
}

std::vector<std::string> EnvoyQuicClientSession::GetAlpnsToOffer() const {
  return configured_alpns_.empty() ? quic::QuicSpdyClientSession::GetAlpnsToOffer()
                                   : configured_alpns_;
}

void EnvoyQuicClientSession::registerNetworkObserver(EnvoyQuicNetworkObserverRegistry& registry) {
  if (network_connectivity_observer_ == nullptr) {
    network_connectivity_observer_ = std::make_unique<QuicNetworkConnectivityObserver>(*this);
  }
  registry.registerObserver(*network_connectivity_observer_);
  registry_ = makeOptRef(registry);
}

} // namespace Quic
} // namespace Envoy
