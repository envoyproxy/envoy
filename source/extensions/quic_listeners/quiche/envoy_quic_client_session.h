#pragma once

#pragma GCC diagnostic push
// QUICHE allows unused parameters.
#pragma GCC diagnostic ignored "-Wunused-parameter"
// QUICHE uses offsetof().
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
#pragma GCC diagnostic ignored "-Wtype-limits"

#include "quiche/quic/core/http/quic_spdy_client_session.h"

#pragma GCC diagnostic pop

#include "extensions/quic_listeners/quiche/envoy_quic_client_stream.h"
#include "extensions/quic_listeners/quiche/quic_filter_manager_connection_impl.h"

namespace Envoy {
namespace Quic {

class EnvoyQuicClientSession : public quic::QuicSpdyClientSession,
                               public QuicFilterManagerConnectionImpl,
                               public Network::ClientConnection {
public:
  EnvoyQuicClientSession(const quic::QuicConfig& config,
                         const quic::ParsedQuicVersionVector& supported_versions,
                         std::unique_ptr<EnvoyQuicConnection> connection,
                         const quic::QuicServerId& server_id,
                         quic::QuicCryptoClientConfig* crypto_config,
                         quic::QuicClientPushPromiseIndex* push_promise_index,
                         Event::Dispatcher& dispatcher, uint32_t send_buffer_limit);

  // Called by QuicHttpClientConnectionImpl before creating data streams.
  void setHttpConnectionCallbacks(Http::ConnectionCallbacks& callbacks) {
    http_connection_callbacks_ = &callbacks;
  }

  // Network::Connection
  absl::string_view requestedServerName() const override;

  // Network::ClientConnection
  void connect() override;

  // quic::QuicSession
  void OnConnectionClosed(const quic::QuicConnectionCloseFrame& frame,
                          quic::ConnectionCloseSource source) override;
  void Initialize() override;
  void OnGoAway(const quic::QuicGoAwayFrame& frame) override;
  // quic::QuicSpdyClientSessionBase
  void OnCryptoHandshakeEvent(CryptoHandshakeEvent event) override;

  using quic::QuicSpdyClientSession::stream_map;

protected:
  // quic::QuicSpdyClientSession
  std::unique_ptr<quic::QuicSpdyClientStream> CreateClientStream() override;
  // quic::QuicSpdySession
  quic::QuicSpdyStream* CreateIncomingStream(quic::QuicStreamId id) override;
  quic::QuicSpdyStream* CreateIncomingStream(quic::PendingStream* pending) override;

private:
  // These callbacks are owned by network filters and quic session should out live
  // them.
  Http::ConnectionCallbacks* http_connection_callbacks_{nullptr};
};

} // namespace Quic
} // namespace Envoy
