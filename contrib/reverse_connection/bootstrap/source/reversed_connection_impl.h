#pragma once

#include "source/common/network/connection_impl.h"
#include "contrib/reverse_connection/bootstrap/source/reverse_conn_global_registry.h"
#include "contrib/reverse_connection/bootstrap/source/reverse_connection_handler.h"

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace ReverseConnection {

class ReversedClientConnectionImpl : public Network::ClientConnectionImpl {
public:
  ReversedClientConnectionImpl(
      Network::Address::InstanceConstSharedPtr address /* remote address */,
      Network::Address::InstanceConstSharedPtr source_address /* source address */,
      Event::Dispatcher& dispatcher,
      Network::TransportSocketPtr&& transport_socket /* transport socket for TLS */,
      Network::ConnectionSocketPtr&& downstream_socket /* client socket to use */,
      Envoy::Extensions::Bootstrap::ReverseConnection::RCThreadLocalRegistry& registry, bool expects_proxy_protocol);

  // Network::ClientConnection
  void connect() override;

  Envoy::Extensions::Bootstrap::ReverseConnection::ReverseConnectionHandler& reverseConnectionHandler() {
    return registry_.getRCHandler();
  }

  // Accessor.
  bool expects_proxy_protocol() const { return expects_proxy_protocol_; }

  void close(Network::ConnectionCloseType type, absl::string_view details) override;

private:
  void SendProxyProtocolHeader();

private:
  Network::Address::InstanceConstSharedPtr remote_address_;
  Network::Address::InstanceConstSharedPtr source_address_;
  Envoy::Extensions::Bootstrap::ReverseConnection::RCThreadLocalRegistry& registry_;
  bool expects_proxy_protocol_;
};

} // namespace ReverseConnection
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
