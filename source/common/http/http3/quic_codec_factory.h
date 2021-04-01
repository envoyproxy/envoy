#pragma once

#include <string>

#include "envoy/config/core/v3/protocol.pb.h"
#include "envoy/config/typed_config.h"
#include "envoy/http/codec.h"
#include "envoy/network/connection.h"

namespace Envoy {
namespace Http {

// A factory to create Http::ServerConnection instance for QUIC.
class QuicHttpServerConnectionFactory : public Config::UntypedFactory {
public:
  ~QuicHttpServerConnectionFactory() override = default;

  virtual std::unique_ptr<ServerConnection>
  createQuicServerConnection(Network::Connection& connection, ConnectionCallbacks& callbacks, const envoy::config::core::v3::Http3ProtocolOptions& http3_options, const uint32_t max_request_headers_kb,
      envoy::config::core::v3::HttpProtocolOptions::HeadersWithUnderscoresAction
          headers_with_underscores_action) PURE;

  std::string category() const override { return "envoy.quic_client_codec"; }
};

// A factory to create Http::ClientConnection instance for QUIC.
class QuicHttpClientConnectionFactory : public Config::UntypedFactory {
public:
  ~QuicHttpClientConnectionFactory() override = default;

  virtual std::unique_ptr<ClientConnection>
  createQuicClientConnection(Network::Connection& connection, ConnectionCallbacks& callbacks, const envoy::config::core::v3::Http3ProtocolOptions& http3_options, const uint32_t max_request_headers_kb) PURE;

  std::string category() const override { return "envoy.quic_server_codec"; }
};

} // namespace Http
} // namespace Envoy
