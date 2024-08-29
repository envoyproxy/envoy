#pragma once

#include <memory>
#include <ostream>

#include "source/common/quic/envoy_quic_connection_debug_visitor_factory_interface.h"
#include "source/common/quic/envoy_quic_server_connection.h"
#include "source/common/quic/envoy_quic_server_crypto_stream_factory.h"
#include "source/common/quic/envoy_quic_server_stream.h"
#include "source/common/quic/quic_filter_manager_connection_impl.h"
#include "source/common/quic/quic_stat_names.h"
#include "source/common/quic/send_buffer_monitor.h"

#include "quiche/quic/core/http/quic_server_session_base.h"
#include "quiche/quic/core/quic_crypto_server_stream.h"
#include "quiche/quic/core/tls_server_handshaker.h"

namespace Envoy {
namespace Quic {

#define QUIC_CONNECTION_STATS(COUNTER)                                                             \
  COUNTER(num_server_migration_detected)                                                           \
  COUNTER(num_packets_rx_on_preferred_address)

struct QuicConnectionStats {
  QUIC_CONNECTION_STATS(GENERATE_COUNTER_STRUCT)
};

using FilterChainToConnectionMap =
    absl::flat_hash_map<const Network::FilterChain*,
                        std::list<std::reference_wrapper<Network::Connection>>>;
using ConnectionMapIter = std::list<std::reference_wrapper<Network::Connection>>::iterator;

// Used to track the matching filter chain and its position in the filter chain to connection map.
struct ConnectionMapPosition {
  ConnectionMapPosition(FilterChainToConnectionMap& connection_map,
                        const Network::FilterChain& filter_chain, ConnectionMapIter iterator)
      : connection_map_(connection_map), filter_chain_(filter_chain), iterator_(iterator) {}

  // Stores the map from filter chain of connections.
  FilterChainToConnectionMap& connection_map_;
  // The matching filter chain of a connection.
  const Network::FilterChain& filter_chain_;
  // The position of the connection in the map.
  ConnectionMapIter iterator_;
};

// Act as a Network::Connection to HCM and a FilterManager to FilterFactoryCb.
// TODO(danzh) Lifetime of quic connection and filter manager connection can be
// simplified by changing the inheritance to a member variable instantiated
// before quic_connection_.
class EnvoyQuicServerSession : public quic::QuicServerSessionBase,
                               public QuicFilterManagerConnectionImpl {
public:
  EnvoyQuicServerSession(
      const quic::QuicConfig& config, const quic::ParsedQuicVersionVector& supported_versions,
      std::unique_ptr<EnvoyQuicServerConnection> connection, quic::QuicSession::Visitor* visitor,
      quic::QuicCryptoServerStreamBase::Helper* helper,
      const quic::QuicCryptoServerConfig* crypto_config,
      quic::QuicCompressedCertsCache* compressed_certs_cache, Event::Dispatcher& dispatcher,
      uint32_t send_buffer_limit, QuicStatNames& quic_stat_names, Stats::Scope& listener_scope,
      EnvoyQuicCryptoServerStreamFactoryInterface& crypto_server_stream_factory,
      std::unique_ptr<StreamInfo::StreamInfo>&& stream_info, QuicConnectionStats& connection_stats,
      EnvoyQuicConnectionDebugVisitorFactoryInterfaceOptRef debug_visitor_factory);

  ~EnvoyQuicServerSession() override;

  // Network::Connection
  absl::string_view requestedServerName() const override;
  void dumpState(std::ostream&, int) const override {
    // TODO(kbaichoo): Implement dumpState for H3.
  }

  // Called by QuicHttpServerConnectionImpl before creating data streams.
  void setHttpConnectionCallbacks(Http::ServerConnectionCallbacks& callbacks) {
    http_connection_callbacks_ = &callbacks;
  }

  // quic::QuicSession
  void OnConnectionClosed(const quic::QuicConnectionCloseFrame& frame,
                          quic::ConnectionCloseSource source) override;
  void Initialize() override;
  void OnCanWrite() override;
  void OnTlsHandshakeComplete() override;
  void OnRstStream(const quic::QuicRstStreamFrame& frame) override;
  void ProcessUdpPacket(const quic::QuicSocketAddress& self_address,
                        const quic::QuicSocketAddress& peer_address,
                        const quic::QuicReceivedPacket& packet) override;
  std::vector<absl::string_view>::const_iterator
  SelectAlpn(const std::vector<absl::string_view>& alpns) const override;

  void setHeadersWithUnderscoreAction(
      envoy::config::core::v3::HttpProtocolOptions::HeadersWithUnderscoresAction
          headers_with_underscores_action) {
    headers_with_underscores_action_ = headers_with_underscores_action;
  }

  void storeConnectionMapPosition(FilterChainToConnectionMap& connection_map,
                                  const Network::FilterChain& filter_chain,
                                  ConnectionMapIter position);

  void setHttp3Options(const envoy::config::core::v3::Http3ProtocolOptions& http3_options) override;
  using quic::QuicSession::PerformActionOnActiveStreams;

protected:
  // quic::QuicServerSessionBase
  std::unique_ptr<quic::QuicCryptoServerStreamBase>
  CreateQuicCryptoServerStream(const quic::QuicCryptoServerConfig* crypto_config,
                               quic::QuicCompressedCertsCache* compressed_certs_cache) override;
  quic::QuicSSLConfig GetSSLConfig() const override;

  // quic::QuicSession
  // Overridden to create stream as encoder and associate it with an decoder.
  quic::QuicSpdyStream* CreateIncomingStream(quic::QuicStreamId id) override;
  quic::QuicSpdyStream* CreateIncomingStream(quic::PendingStream* pending) override;
  quic::QuicSpdyStream* CreateOutgoingBidirectionalStream() override;
  quic::QuicSpdyStream* CreateOutgoingUnidirectionalStream() override;

  quic::HttpDatagramSupport LocalHttpDatagramSupport() override { return http_datagram_support_; }

  // QuicFilterManagerConnectionImpl
  bool hasDataToWrite() override;
  // Used by base class to access quic connection after initialization.
  const quic::QuicConnection* quicConnection() const override;
  quic::QuicConnection* quicConnection() override;

private:
  void setUpRequestDecoder(EnvoyQuicServerStream& stream);

  std::unique_ptr<EnvoyQuicServerConnection> quic_connection_;
  // These callbacks are owned by network filters and quic session should out live
  // them.
  Http::ServerConnectionCallbacks* http_connection_callbacks_{nullptr};

  envoy::config::core::v3::HttpProtocolOptions::HeadersWithUnderscoresAction
      headers_with_underscores_action_;

  EnvoyQuicCryptoServerStreamFactoryInterface& crypto_server_stream_factory_;
  absl::optional<ConnectionMapPosition> position_;
  QuicConnectionStats& connection_stats_;
  quic::HttpDatagramSupport http_datagram_support_ = quic::HttpDatagramSupport::kNone;
  std::unique_ptr<quic::QuicConnectionDebugVisitor> debug_visitor_;
};

} // namespace Quic
} // namespace Envoy
