#pragma once

#include "envoy/http/codec.h"
#include "envoy/registry/registry.h"

#include "source/common/common/assert.h"
#include "source/common/common/logger.h"
#include "source/common/quic/envoy_quic_client_session.h"
#include "source/common/quic/envoy_quic_server_session.h"

namespace Envoy {
namespace Quic {

// QuicHttpConnectionImplBase instance is a thin QUIC codec just providing quic interface to HCM.
// Owned by HCM and created during onNewConnection() if the network connection
// is a QUIC connection.
class QuicHttpConnectionImplBase : public virtual Http::Connection,
                                   protected Logger::Loggable<Logger::Id::quic> {
public:
  QuicHttpConnectionImplBase(QuicFilterManagerConnectionImpl& quic_session,
                             Http::Http3::CodecStats& stats)
      : quic_session_(quic_session), stats_(stats) {}

  // Http::Connection
  Http::Status dispatch(Buffer::Instance& /*data*/) override {
    // Bypassed. QUIC connection already hands all data to streams.
    PANIC("not implemented");
  }
  Http::Protocol protocol() override { return Http::Protocol::Http3; }
  // Returns true if the session has data to send but queued in connection or
  // stream send buffer.
  bool wantsToWrite() override;

protected:
  QuicFilterManagerConnectionImpl& quic_session_;
  Http::Http3::CodecStats& stats_;
};

class QuicHttpServerConnectionImpl : public QuicHttpConnectionImplBase,
                                     public Http::ServerConnection {
public:
  QuicHttpServerConnectionImpl(
      EnvoyQuicServerSession& quic_session, Http::ServerConnectionCallbacks& callbacks,
      Http::Http3::CodecStats& stats,
      const envoy::config::core::v3::Http3ProtocolOptions& http3_options,
      const uint32_t max_request_headers_kb, const uint32_t max_request_headers_count,
      envoy::config::core::v3::HttpProtocolOptions::HeadersWithUnderscoresAction
          headers_with_underscores_action);

  // Http::Connection
  void goAway() override;
  void shutdownNotice() override;
  void onUnderlyingConnectionAboveWriteBufferHighWatermark() override;
  void onUnderlyingConnectionBelowWriteBufferLowWatermark() override;

  EnvoyQuicServerSession& quicServerSession() { return quic_server_session_; }

private:
  EnvoyQuicServerSession& quic_server_session_;
};

class QuicHttpClientConnectionImpl : public QuicHttpConnectionImplBase,
                                     public Http::ClientConnection {
public:
  QuicHttpClientConnectionImpl(EnvoyQuicClientSession& session,
                               Http::ConnectionCallbacks& callbacks, Http::Http3::CodecStats& stats,
                               const envoy::config::core::v3::Http3ProtocolOptions& http3_options,
                               const uint32_t max_request_headers_kb,
                               const uint32_t max_response_headers_count);

  // Http::ClientConnection
  Http::RequestEncoder& newStream(Http::ResponseDecoder& response_decoder) override;

  // Http::Connection
  void goAway() override;
  void shutdownNotice() override {}
  void onUnderlyingConnectionAboveWriteBufferHighWatermark() override;
  void onUnderlyingConnectionBelowWriteBufferLowWatermark() override;

private:
  EnvoyQuicClientSession& quic_client_session_;
};

} // namespace Quic
} // namespace Envoy
