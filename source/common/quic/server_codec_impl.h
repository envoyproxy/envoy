#pragma once

#include "envoy/http/codec.h"
#include "envoy/registry/registry.h"

#include "source/common/common/assert.h"
#include "source/common/common/logger.h"
#include "source/common/quic/codec_impl.h"
#include "source/common/quic/envoy_quic_server_session.h"
#include "source/common/quic/server_connection_factory.h"

namespace Envoy {
namespace Quic {

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

class QuicHttpServerConnectionFactoryImpl : public QuicHttpServerConnectionFactory {
public:
  std::unique_ptr<Http::ServerConnection> createQuicHttpServerConnectionImpl(
      Network::Connection& connection, Http::ServerConnectionCallbacks& callbacks,
      Http::Http3::CodecStats& stats,
      const envoy::config::core::v3::Http3ProtocolOptions& http3_options,
      const uint32_t max_request_headers_kb, const uint32_t max_request_headers_count,
      envoy::config::core::v3::HttpProtocolOptions::HeadersWithUnderscoresAction
          headers_with_underscores_action) override {
    return std::make_unique<QuicHttpServerConnectionImpl>(
        dynamic_cast<Quic::EnvoyQuicServerSession&>(connection), callbacks, stats, http3_options,
        max_request_headers_kb, max_request_headers_count, headers_with_underscores_action);
  }
  std::string name() const override { return "quic.http_server_connection.default"; }
};

DECLARE_FACTORY(QuicHttpServerConnectionFactoryImpl);

} // namespace Quic
} // namespace Envoy
