#pragma once

#include "source/common/network/connection_impl.h"

namespace Envoy {
namespace Network {

class ReversedClientConnectionImpl : public ClientConnectionImpl {
public:
  ReversedClientConnectionImpl(
      Network::Address::InstanceConstSharedPtr address /* remote address */,
      Network::Address::InstanceConstSharedPtr source_address /* source address */,
      Event::Dispatcher& dispatcher,
      Network::TransportSocketPtr&& transport_socket /* transport socket for TLS */,
      Network::ConnectionSocketPtr&& downstream_socket /* client socket to use */,
      bool expects_proxy_protocol);

  // Network::ClientConnection
  void connect() override;

  // Accessor.
  bool expects_proxy_protocol() const { return expects_proxy_protocol_; }

  void close(ConnectionCloseType type, absl::string_view details) override;

private:
  void SendProxyProtocolHeader();

private:
  Network::Address::InstanceConstSharedPtr remote_address_;
  Network::Address::InstanceConstSharedPtr source_address_;
  bool expects_proxy_protocol_;
};

} // namespace Network
} // namespace Envoy
