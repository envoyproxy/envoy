#include "source/common/quic/envoy_quic_client_session.h"

#include "source/common/quic/envoy_quic_utils.h"

#include "quic_filter_manager_connection_impl.h"

namespace quic {
namespace test {

// TODO(alyssawilk) add the necessary accessors to quiche and remove this.
class QuicSessionPeer {
public:
  static quic::QuicStreamIdManager&
  getStreamIdManager(Envoy::Quic::EnvoyQuicClientSession* session) {
    return session->ietf_streamid_manager_.bidirectional_stream_id_manager_;
  }
};

} // namespace test
} // namespace quic

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
  dynamic_cast<EnvoyQuicClientConnection*>(network_connection_)
      ->setUpConnectionSocket(
          *static_cast<EnvoyQuicClientConnection*>(connection())->connectionSocket(), *this);
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
  quic::QuicSpdyClientSession::OnCanWrite();
  maybeApplyDelayClosePolicy();
}

void EnvoyQuicClientSession::OnHttp3GoAway(uint64_t stream_id) {
  ENVOY_CONN_LOG(debug, "HTTP/3 GOAWAY received", *this);
  quic::QuicSpdyClientSession::OnHttp3GoAway(stream_id);
  if (http_connection_callbacks_ != nullptr) {
    // HTTP/3 GOAWAY doesn't have an error code field.
    http_connection_callbacks_->onGoAway(Http::GoAwayErrorCode::NoError);
  }
}

void EnvoyQuicClientSession::MaybeSendRstStreamFrame(quic::QuicStreamId id,
                                                     quic::QuicResetStreamError error,
                                                     quic::QuicStreamOffset bytes_written) {
  QuicSpdyClientSession::MaybeSendRstStreamFrame(id, error, bytes_written);
  quic_stat_names_.chargeQuicResetStreamErrorStats(scope_, error, /*from_self*/ true,
                                                   /*is_upstream*/ true);
}

void EnvoyQuicClientSession::OnRstStream(const quic::QuicRstStreamFrame& frame) {
  QuicSpdyClientSession::OnRstStream(frame);
  quic_stat_names_.chargeQuicResetStreamErrorStats(scope_, frame.error(),
                                                   /*from_self*/ false, /*is_upstream*/ true);
}

void EnvoyQuicClientSession::OnCanCreateNewOutgoingStream(bool unidirectional) {
  if (!http_connection_callbacks_ || unidirectional) {
    return;
  }
  uint32_t streams_available = streamsAvailable();
  if (streams_available > 0) {
    http_connection_callbacks_->onMaxStreamsChanged(streams_available);
  }
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
  NOT_REACHED_GCOVR_EXCL_LINE;
}

bool EnvoyQuicClientSession::hasDataToWrite() { return HasDataToWrite(); }

const quic::QuicConnection* EnvoyQuicClientSession::quicConnection() const {
  return initialized_ ? connection() : nullptr;
}

quic::QuicConnection* EnvoyQuicClientSession::quicConnection() {
  return initialized_ ? connection() : nullptr;
}

uint64_t EnvoyQuicClientSession::streamsAvailable() {
  quic::QuicStreamIdManager& manager = quic::test::QuicSessionPeer::getStreamIdManager(this);
  ASSERT(manager.outgoing_max_streams() >= manager.outgoing_stream_count());
  uint32_t streams_available = manager.outgoing_max_streams() - manager.outgoing_stream_count();
  return streams_available;
}

void EnvoyQuicClientSession::OnTlsHandshakeComplete() {
  quic::QuicSpdyClientSession::OnTlsHandshakeComplete();

  // TODO(alyssawilk) support the case where a connection starts with 0 max streams.
  ASSERT(streamsAvailable());
  if (streamsAvailable() > 0) {
    OnCanCreateNewOutgoingStream(false);
    raiseConnectionEvent(Network::ConnectionEvent::Connected);
  }
}

std::unique_ptr<quic::QuicCryptoClientStreamBase> EnvoyQuicClientSession::CreateQuicCryptoStream() {
  return crypto_stream_factory_.createEnvoyQuicCryptoClientStream(
      server_id(), this, crypto_config()->proof_verifier()->CreateDefaultContext(), crypto_config(),
      this, /*has_application_state = */ version().UsesHttp3());
}

void EnvoyQuicClientSession::setHttp3Options(
    const envoy::config::core::v3::Http3ProtocolOptions& http3_options) {
  QuicFilterManagerConnectionImpl::setHttp3Options(http3_options);
  if (http3_options_->has_quic_protocol_options()) {
    static_cast<EnvoyQuicClientConnection*>(connection())
        ->setMigratePortOnPathDegrading(PROTOBUF_GET_WRAPPED_OR_DEFAULT(
            http3_options.quic_protocol_options(), num_timeouts_to_trigger_port_migration, 1));
  }
}

} // namespace Quic
} // namespace Envoy
