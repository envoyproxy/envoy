#pragma once

#pragma GCC diagnostic push
// QUICHE allows unused parameters.
#pragma GCC diagnostic ignored "-Wunused-parameter"
// QUICHE uses offsetof().
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
#pragma GCC diagnostic ignored "-Wtype-limits"

#include "quiche/quic/core/http/quic_server_session_base.h"

#pragma GCC diagnostic pop

#include <memory>

#include "extensions/quic_listeners/quiche/quic_filter_manager_connection_impl.h"
#include "extensions/quic_listeners/quiche/envoy_quic_stream.h"

namespace Envoy {
namespace Quic {

// Act as a Network::Connection to HCM and a FilterManager to FilterFactoryCb.
class EnvoyQuicServerSession : public quic::QuicServerSessionBase,
                               public QuicFilterManagerConnectionImpl {
public:
  EnvoyQuicServerSession(const quic::QuicConfig& config,
                         const quic::ParsedQuicVersionVector& supported_versions,
                         std::unique_ptr<EnvoyQuicConnection> connection,
                         quic::QuicSession::Visitor* visitor,
                         quic::QuicCryptoServerStream::Helper* helper,
                         const quic::QuicCryptoServerConfig* crypto_config,
                         quic::QuicCompressedCertsCache* compressed_certs_cache,
                         Event::Dispatcher& dispatcher);

  // Network::Connection
  absl::string_view requestedServerName() const override;

  // Called by QuicHttpServerConnectionImpl before creating data streams.
  void setHttpConnectionCallbacks(Http::ServerConnectionCallbacks& callbacks) {
    http_connection_callbacks_ = &callbacks;
  }

  // quic::QuicSession
  void OnConnectionClosed(const quic::QuicConnectionCloseFrame& frame,
                          quic::ConnectionCloseSource source) override;
  void Initialize() override;
  void SendGoAway(quic::QuicErrorCode error_code, const std::string& reason) override;
  // quic::QuicSpdySession
  void OnCryptoHandshakeEvent(CryptoHandshakeEvent event) override;

protected:
  // quic::QuicServerSessionBase
  quic::QuicCryptoServerStreamBase*
  CreateQuicCryptoServerStream(const quic::QuicCryptoServerConfig* crypto_config,
                               quic::QuicCompressedCertsCache* compressed_certs_cache) override;

  // quic::QuicSession
  // Overridden to create stream as encoder and associate it with an decoder.
  quic::QuicSpdyStream* CreateIncomingStream(quic::QuicStreamId id) override;
  quic::QuicSpdyStream* CreateIncomingStream(quic::PendingStream* pending) override;
  quic::QuicSpdyStream* CreateOutgoingBidirectionalStream() override;
  quic::QuicSpdyStream* CreateOutgoingUnidirectionalStream() override;

private:
  void setUpRequestDecoder(EnvoyQuicStream& stream);

  // These callbacks are owned by network filters and quic session should out live
  // them.
  Http::ServerConnectionCallbacks* http_connection_callbacks_{nullptr};
};

} // namespace Quic
} // namespace Envoy
