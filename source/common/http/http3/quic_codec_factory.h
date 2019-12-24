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

  const std::string category() const override { return "quic_client_codec"; }

  // Prevents double registration for the same config proto
  // const std::string configType() override { return ""; }
};

// A factory to create Http::ClientConnection instance for QUIC.
class QuicHttpClientConnectionFactory : public Config::UntypedFactory {
public:
  virtual ~QuicHttpClientConnectionFactory() {}

  virtual std::unique_ptr<ClientConnection>
  createQuicClientConnection(Network::Connection& connection, ConnectionCallbacks& callbacks) PURE;

  const std::string category() const override { return "quic_server_codec"; }

  // Prevents double registration for the same config proto
  // const std::string configType() override { return ""; }
};

} // namespace Http
} // namespace Envoy
