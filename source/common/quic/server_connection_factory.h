#pragma once

#include "envoy/config/core/v3/protocol.pb.h"
#include "envoy/http/codec.h"
#include "envoy/network/connection.h"

#include "source/common/http/http3/codec_stats.h"

namespace Envoy {
class QuicHttpServerConnectionFactory : public Config::UntypedFactory {
public:
  virtual std::unique_ptr<Http::ServerConnection> createQuicHttpServerConnectionImpl(
      Network::Connection& connection, Http::ServerConnectionCallbacks& callbacks,
      Http::Http3::CodecStats& stats,
      const envoy::config::core::v3::Http3ProtocolOptions& http3_options,
      const uint32_t max_request_headers_kb, const uint32_t max_request_headers_count,
      envoy::config::core::v3::HttpProtocolOptions::HeadersWithUnderscoresAction
          headers_with_underscores_action,
      Server::OverloadManager& overload_manager) PURE;

  std::string category() const override { return "quic.http_server_connection"; }
};
} // namespace Envoy
