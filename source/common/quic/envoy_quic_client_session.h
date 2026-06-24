#pragma once

#include "envoy/http/http_server_properties_cache.h"

#include "source/common/quic/envoy_quic_client_connection.h"
#include "source/common/quic/envoy_quic_client_crypto_stream_factory.h"
#include "source/common/quic/envoy_quic_client_stream.h"
#include "source/common/quic/envoy_quic_network_observer_registry_factory.h"
#include "source/common/quic/quic_filter_manager_connection_impl.h"
#include "source/common/quic/quic_network_connectivity_observer.h"
#include "source/common/quic/quic_stat_names.h"
#include "source/common/quic/quic_transport_socket_factory.h"
#include "source/common/quic/scone_state.h"

#include "absl/container/flat_hash_set.h"
#include "quiche/quic/core/http/quic_spdy_client_session.h"

namespace Envoy {
namespace Quic {

// Act as a Network::ClientConnection to ClientCodec.
// TODO(danzh) This class doesn't need to inherit Network::FilterManager
// interface but need all other Network::Connection implementation in
// QuicFilterManagerConnectionImpl. Refactor QuicFilterManagerConnectionImpl to
// move FilterManager interface to EnvoyQuicServerSession.
class EnvoyQuicClientSession : public QuicFilterManagerConnectionImpl,
                               public quic::QuicSpdyClientSession,
                               public Network::ClientConnection,
                               public PacketsToReadDelegate {
public:
  EnvoyQuicClientSession(
      const quic::QuicConfig& config, const quic::ParsedQuicVersionVector& supported_versions,
      std::unique_ptr<EnvoyQuicClientConnection> connection,
      quic::QuicForceBlockablePacketWriter* absl_nullable writer,
      EnvoyQuicClientConnection::EnvoyQuicMigrationHelper* absl_nullable migration_helper,
      const quic::QuicConnectionMigrationConfig& migration_config,
      const quic::QuicServerId& server_id,
      std::shared_ptr<quic::QuicCryptoClientConfig> crypto_config, Event::Dispatcher& dispatcher,
      uint32_t send_buffer_limit,
      EnvoyQuicCryptoClientStreamFactoryInterface& crypto_stream_factory,
      QuicStatNames& quic_stat_names, OptRef<Http::HttpServerPropertiesCache> rtt_cache,
      Stats::Scope& scope,
      const Network::TransportSocketOptionsConstSharedPtr& transport_socket_options,
      OptRef<Network::UpstreamTransportSocketFactory> transport_socket_factory);

  ~EnvoyQuicClientSession() override;

  // Called by QuicHttpClientConnectionImpl before creating data streams.
  void setHttpConnectionCallbacks(Http::ConnectionCallbacks& callbacks) {
    http_connection_callbacks_ = &callbacks;
  }

  // Network::Connection
  absl::string_view requestedServerName() const override;
  void dumpState(std::ostream&, int) const override {
    // TODO(kbaichoo): Implement dumpState for H3.
  }

  // Network::ClientConnection
  // Set up socket and start handshake.
  void connect() override;

  bool setSocketOption(Envoy::Network::SocketOptionName, absl::Span<uint8_t>) override {
    return false;
  }

  // quic::QuicSession
  void OnConnectionClosed(const quic::QuicConnectionCloseFrame& frame,
                          quic::ConnectionCloseSource source) override;
  void Initialize() override;
  void OnCanWrite() override;
  void OnHttp3GoAway(uint64_t stream_id) override;
  void OnTlsHandshakeComplete() override;
  void OnRstStream(const quic::QuicRstStreamFrame& frame) override;
  void OnNewEncryptionKeyAvailable(quic::EncryptionLevel level,
                                   std::unique_ptr<quic::QuicEncrypter> encrypter) override;

  // quic::QuicClientSessionWithMigration
  void StartDraining() override;

  quic::HttpDatagramSupport LocalHttpDatagramSupport() override { return http_datagram_support_; }

#ifdef ENVOY_ENABLE_HTTP_DATAGRAMS
  quic::WebTransportHttp3VersionSet LocallySupportedWebTransportVersions() const override;
#endif
  std::vector<std::string> GetAlpnsToOffer() const override;
  void OnConfigNegotiated() override;
  void OnSconePacket(quic::QuicBandwidth bandwidth) override;

  // quic::QuicSpdySession
  // Overridden to flush WebTransport CONNECT requests that were deferred until the peer's HTTP/3
  // SETTINGS arrived. See registerStreamWaitingForSettings().
  bool OnSettingsFrame(const quic::SettingsFrame& frame) override;

#ifdef ENVOY_ENABLE_HTTP_DATAGRAMS
  // Registers/unregisters a client stream that buffered a WebTransport CONNECT request in
  // encodeHeaders() because the peer's HTTP/3 SETTINGS had not yet been received. QUICHE creates
  // the upstream WebTransport session synchronously inside WriteHeaders, but only if WebTransport
  // is already negotiated (which is known only after SETTINGS). When the SETTINGS frame arrives,
  // OnSettingsFrame() flushes every registered stream. Streams are tracked by ID rather than
  // pointer: a stream that is destroyed without unregistering simply fails the GetActiveStream()
  // lookup at flush time, so there is no dangling reference. Only WebTransport CONNECT streams ever
  // wait here; all other requests are written immediately and the connection becomes ready right
  // after the handshake.
  void registerStreamWaitingForSettings(quic::QuicStreamId stream_id);
  void unregisterStreamWaitingForSettings(quic::QuicStreamId stream_id);
#endif

  // quic::QuicSpdyClientSessionBase
  bool ShouldKeepConnectionAlive() const override;
  // quic::ProofHandler
  void OnProofVerifyDetailsAvailable(const quic::ProofVerifyDetails& verify_details) override;

  // PacketsToReadDelegate
  size_t numPacketsExpectedPerEventLoop() const override {
    // Do one round of reading per active stream, or to see if there's a new active stream.
    return std::max<size_t>(1, GetNumActiveStreams()) * Network::NUM_DATAGRAMS_PER_RECEIVE;
  }

  // QuicFilterManagerConnectionImpl
  void setHttp3Options(const envoy::config::core::v3::Http3ProtocolOptions& http3_options) override;

  // Notify any registered connection pool when new streams are available.
  void OnCanCreateNewOutgoingStream(bool) override;

  void OnServerPreferredAddressAvailable(
      const quic::QuicSocketAddress& server_preferred_address) override;

  // Register this session to the given registry for receiving network change events.
  void registerNetworkObserver(EnvoyQuicNetworkObserverRegistry& registry);

  // NOLINTNEXTLINE(readability-identifier-naming)
  const quic::TransportParameters::ParameterMap& received_custom_transport_parameters() {
    return received_custom_transport_parameters_;
  }
  // NOLINTNEXTLINE(readability-identifier-naming)
  const absl::optional<quic::QuicSocketAddress>& received_ipv6_alternate_server_address() {
    return received_ipv6_alternate_server_address_;
  }
  // NOLINTNEXTLINE(readability-identifier-naming)
  const absl::optional<quic::QuicSocketAddress>& received_ipv4_alternate_server_address() {
    return received_ipv4_alternate_server_address_;
  }

  using quic::QuicSpdyClientSession::PerformActionOnActiveStreams;

protected:
  // quic::QuicSpdyClientSession
  std::unique_ptr<quic::QuicSpdyClientStream> CreateClientStream() override;
  // quic::QuicSpdySession
  quic::QuicSpdyStream* CreateIncomingStream(quic::QuicStreamId id) override;
  std::unique_ptr<quic::QuicCryptoClientStreamBase> CreateQuicCryptoStream() override;
  bool ShouldCreateOutgoingBidirectionalStream() override {
    // quic::QuicSpdyClientSession::ShouldCreateOutgoingBidirectionalStream()
    // might return false, but we want to create the stream anyway
    // because otherwise we crash dereferencing a nullptr, so we
    // don't even ask it, and just return true.
    return true;
  }
  // QuicFilterManagerConnectionImpl
  bool hasDataToWrite() override;
  // Used by base class to access quic connection after initialization.
  const quic::QuicConnection* quicConnection() const override;
  quic::QuicConnection* quicConnection() override;

private:
  uint64_t streamsAvailable();

#ifdef ENVOY_ENABLE_HTTP_DATAGRAMS
  // IDs of client streams that buffered a WebTransport CONNECT request awaiting the peer's HTTP/3
  // SETTINGS. Flushed and cleared in OnSettingsFrame(). See registerStreamWaitingForSettings().
  absl::flat_hash_set<quic::QuicStreamId> streams_waiting_for_settings_;
#endif

  // These callbacks are owned by network filters and quic session should outlive
  // them.
  Http::ConnectionCallbacks* http_connection_callbacks_{nullptr};
  std::shared_ptr<quic::QuicCryptoClientConfig> crypto_config_;
  EnvoyQuicCryptoClientStreamFactoryInterface& crypto_stream_factory_;
  OptRef<Http::HttpServerPropertiesCache> rtt_cache_;
  bool disable_keepalive_{false};
  Network::TransportSocketOptionsConstSharedPtr transport_socket_options_;
  OptRef<QuicTransportSocketFactoryBase> transport_socket_factory_;
  std::vector<std::string> configured_alpns_;
  quic::HttpDatagramSupport http_datagram_support_ = quic::HttpDatagramSupport::kNone;
  const bool session_handles_migration_;
  QuicNetworkConnectivityObserverPtr network_connectivity_observer_;
  OptRef<EnvoyQuicNetworkObserverRegistry> registry_;
  quic::TransportParameters::ParameterMap received_custom_transport_parameters_;
  absl::optional<quic::QuicSocketAddress> received_ipv6_alternate_server_address_;
  absl::optional<quic::QuicSocketAddress> received_ipv4_alternate_server_address_;
};

} // namespace Quic
} // namespace Envoy
