#include "source/common/quic/envoy_quic_server_session.h"

#include <iterator>
#include <memory>
#include <type_traits>

#include "source/common/common/assert.h"
#include "source/common/common/scope_tracker.h"
#include "source/common/quic/envoy_quic_connection_debug_visitor_factory_interface.h"
#include "source/common/quic/envoy_quic_proof_source.h"
#include "source/common/quic/envoy_quic_server_stream.h"
#include "source/common/quic/quic_filter_manager_connection_impl.h"

#include "absl/types/optional.h"

namespace Envoy {
namespace Quic {

namespace {
class EnvoyQuicConnectionContextListener : public quic::QuicConnectionContextListener {
public:
  EnvoyQuicConnectionContextListener(const ScopeTrackedObject* object, Event::ScopeTracker& tracker)
      : object_(object), tracker_(tracker) {}

private:
  void Activate() override { state_.emplace(object_, tracker_); }
  void Deactivate() override { state_.reset(); }

  const ScopeTrackedObject* object_;
  Event::ScopeTracker& tracker_;
  absl::optional<ScopeTrackerScopeState> state_;
};
} // namespace

EnvoyQuicServerSession::EnvoyQuicServerSession(
    const quic::QuicConfig& config, const quic::ParsedQuicVersionVector& supported_versions,
    std::unique_ptr<EnvoyQuicServerConnection> connection, quic::QuicSession::Visitor* visitor,
    quic::QuicCryptoServerStream::Helper* helper, const quic::QuicCryptoServerConfig* crypto_config,
    quic::QuicCompressedCertsCache* compressed_certs_cache, Event::Dispatcher& dispatcher,
    uint32_t send_buffer_limit, QuicStatNames& quic_stat_names, Stats::Scope& listener_scope,
    EnvoyQuicCryptoServerStreamFactoryInterface& crypto_server_stream_factory,
    std::unique_ptr<StreamInfo::StreamInfo>&& stream_info, QuicConnectionStats& connection_stats,
    EnvoyQuicConnectionDebugVisitorFactoryInterfaceOptRef debug_visitor_factory)
    : quic::QuicServerSessionBase(config, supported_versions, connection.get(), visitor, helper,
                                  crypto_config, compressed_certs_cache),
      QuicFilterManagerConnectionImpl(*connection, connection->connection_id(), dispatcher,
                                      send_buffer_limit,
                                      std::make_shared<QuicSslConnectionInfo>(*this),
                                      std::move(stream_info), quic_stat_names, listener_scope),
      quic_connection_(std::move(connection)),
      crypto_server_stream_factory_(crypto_server_stream_factory),
      connection_stats_(connection_stats) {
#ifdef ENVOY_ENABLE_HTTP_DATAGRAMS
  http_datagram_support_ = quic::HttpDatagramSupport::kRfc;
#endif
  // If a factory is available, create a debug visitor and attach it to the connection.
  if (debug_visitor_factory.has_value()) {
    debug_visitor_ = debug_visitor_factory->createQuicConnectionDebugVisitor(this, streamInfo());
    quic_connection_->set_debug_visitor(debug_visitor_.get());
  }
  quic_connection_->set_context_listener(
      std::make_unique<EnvoyQuicConnectionContextListener>(this, dispatcher));
}

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
      crypto_config, compressed_certs_cache, this, stream_helper(),
      makeOptRefFromPtr(position_.has_value() ? &position_->filter_chain_.transportSocketFactory()
                                              : nullptr),
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
  IS_ENVOY_BUG("Unexpected disallowed server push call");
  return nullptr;
}

quic::QuicSpdyStream* EnvoyQuicServerSession::CreateOutgoingBidirectionalStream() {
  IS_ENVOY_BUG("Unexpected disallowed server initiated stream");
  return nullptr;
}

quic::QuicSpdyStream* EnvoyQuicServerSession::CreateOutgoingUnidirectionalStream() {
  IS_ENVOY_BUG("Unexpected function call");
  return nullptr;
}

void EnvoyQuicServerSession::setUpRequestDecoder(EnvoyQuicServerStream& stream) {
  ASSERT(http_connection_callbacks_ != nullptr);
  Http::RequestDecoder& decoder = http_connection_callbacks_->newStream(stream);
  stream.setRequestDecoder(decoder);
}

void EnvoyQuicServerSession::OnConnectionClosed(const quic::QuicConnectionCloseFrame& frame,
                                                quic::ConnectionCloseSource source) {
  quic::QuicServerSessionBase::OnConnectionClosed(frame, source);
  if (source == quic::ConnectionCloseSource::FROM_SELF) {
    setLocalCloseReason(frame.error_details);
  }
  onConnectionCloseEvent(frame, source, version());
  if (position_.has_value()) {
    // Remove this connection from the map.
    std::list<std::reference_wrapper<Network::Connection>>& connections =
        position_->connection_map_[&position_->filter_chain_];
    connections.erase(position_->iterator_);
    if (connections.empty()) {
      // Remove the whole entry if this is the last connection using this filter chain.
      position_->connection_map_.erase(&position_->filter_chain_);
    }
    position_.reset();
  }
}

void EnvoyQuicServerSession::Initialize() {
  quic::QuicServerSessionBase::Initialize();
  initialized_ = true;
  quic_connection_->setEnvoyConnection(*this, *this);
}

void EnvoyQuicServerSession::OnCanWrite() {
  uint64_t old_bytes_to_send = bytesToSend();
  quic::QuicServerSessionBase::OnCanWrite();
  // Do not update delay close timer according to connection level packet egress because that is
  // equivalent to TCP transport layer egress. But only do so if the session gets chance to write.
  const bool has_sent_any_data = bytesToSend() != old_bytes_to_send;
  maybeUpdateDelayCloseTimer(has_sent_any_data);
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
  streamInfo().downstreamTiming().onDownstreamHandshakeComplete(dispatcher_.timeSource());
  raiseConnectionEvent(Network::ConnectionEvent::Connected);
}

void EnvoyQuicServerSession::OnRstStream(const quic::QuicRstStreamFrame& frame) {
  QuicServerSessionBase::OnRstStream(frame);
  incrementSentQuicResetStreamErrorStats(frame.error(),
                                         /*from_self*/ false, /*is_upstream*/ false);
}

void EnvoyQuicServerSession::setHttp3Options(
    const envoy::config::core::v3::Http3ProtocolOptions& http3_options) {
  QuicFilterManagerConnectionImpl::setHttp3Options(http3_options);
  if (http3_options_->has_quic_protocol_options() &&
      http3_options_->quic_protocol_options().has_connection_keepalive()) {
    const uint64_t initial_interval = PROTOBUF_GET_MS_OR_DEFAULT(
        http3_options_->quic_protocol_options().connection_keepalive(), initial_interval, 0);
    const uint64_t max_interval =
        PROTOBUF_GET_MS_OR_DEFAULT(http3_options_->quic_protocol_options().connection_keepalive(),
                                   max_interval, quic::kPingTimeoutSecs);
    if (max_interval == 0) {
      return;
    }
    if (initial_interval > 0) {
      connection()->set_keep_alive_ping_timeout(
          quic::QuicTime::Delta::FromMilliseconds(max_interval));
      connection()->set_initial_retransmittable_on_wire_timeout(
          quic::QuicTime::Delta::FromMilliseconds(initial_interval));
    }
  }
  set_allow_extended_connect(http3_options_->allow_extended_connect());
}

void EnvoyQuicServerSession::storeConnectionMapPosition(FilterChainToConnectionMap& connection_map,
                                                        const Network::FilterChain& filter_chain,
                                                        ConnectionMapIter position) {
  position_.emplace(connection_map, filter_chain, position);
}

quic::QuicSSLConfig EnvoyQuicServerSession::GetSSLConfig() const {
  quic::QuicSSLConfig config = quic::QuicServerSessionBase::GetSSLConfig();
  config.early_data_enabled = position_.has_value()
                                  ? dynamic_cast<const QuicServerTransportSocketFactory&>(
                                        position_->filter_chain_.transportSocketFactory())
                                        .earlyDataEnabled()
                                  : true;
  return config;
}

void EnvoyQuicServerSession::ProcessUdpPacket(const quic::QuicSocketAddress& self_address,
                                              const quic::QuicSocketAddress& peer_address,
                                              const quic::QuicReceivedPacket& packet) {
  // If L4 filters causes the connection to be closed early during initialization, now
  // is the time to actually close the connection.
  maybeHandleCloseDuringInitialize();
  quic::QuicServerSessionBase::ProcessUdpPacket(self_address, peer_address, packet);
  if (connection()->expected_server_preferred_address().IsInitialized() &&
      self_address == connection()->expected_server_preferred_address()) {
    connection_stats_.num_packets_rx_on_preferred_address_.inc();
  }
  maybeApplyDelayedClose();
}

std::vector<absl::string_view>::const_iterator
EnvoyQuicServerSession::SelectAlpn(const std::vector<absl::string_view>& alpns) const {
  if (!position_.has_value()) {
    return quic::QuicServerSessionBase::SelectAlpn(alpns);
  }
  const std::vector<absl::string_view>& configured_alpns =
      dynamic_cast<const QuicServerTransportSocketFactory&>(
          position_->filter_chain_.transportSocketFactory())
          .supportedAlpnProtocols();
  if (configured_alpns.empty()) {
    return quic::QuicServerSessionBase::SelectAlpn(alpns);
  }

  for (absl::string_view configured_alpn : configured_alpns) {
    auto it = absl::c_find(alpns, configured_alpn);
    if (it != alpns.end()) {
      return it;
    }
  }
  return alpns.end();
}

} // namespace Quic
} // namespace Envoy
