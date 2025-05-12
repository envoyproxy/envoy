#pragma once

#include "envoy/http/codec.h"
#include "envoy/registry/registry.h"

#include "source/common/common/assert.h"
#include "source/common/common/logger.h"
#include "source/common/quic/codec_impl.h"
#include "source/common/quic/envoy_quic_client_session.h"

namespace Envoy {
namespace Quic {

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
