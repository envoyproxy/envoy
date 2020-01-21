#pragma once

#include <string>

#include "envoy/config/typed_config.h"
#include "envoy/http/codec.h"
#include "envoy/network/connection.h"

namespace Envoy {
namespace Http {

// A factory to create Http::ServerConnection instance for QUIC.
class QuicHttpServerConnectionFactory : public Config::UntypedFactory {
public:
  virtual ~QuicHttpServerConnectionFactory() {}

  virtual std::unique_ptr<ServerConnection>
  createQuicServerConnection(Network::Connection& connection, ConnectionCallbacks& callbacks) PURE;

  std::string category() const override { return "envoy.quic_client_codec"; }
};

// A factory to create Http::ClientConnection instance for QUIC.
class QuicHttpClientConnectionFactory : public Config::UntypedFactory {
public:
  virtual ~QuicHttpClientConnectionFactory() {}

  virtual std::unique_ptr<ClientConnection>
  createQuicClientConnection(Network::Connection& connection, ConnectionCallbacks& callbacks) PURE;

  std::string category() const override { return "envoy.quic_server_codec"; }
};

} // namespace Http
} // namespace Envoy
