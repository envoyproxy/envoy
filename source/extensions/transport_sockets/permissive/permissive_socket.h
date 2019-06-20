#pragma once

#include "envoy/buffer/buffer.h"
#include "envoy/network/connection.h"
#include "envoy/network/transport_socket.h"

#include "common/common/logger.h"

#include "extensions/transport_sockets/permissive/proxy_transport_socket_callbacks.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Permissive {

/**
 * Transport socket wrapper for ssl_socket and raw_buffer_socket.
 */
class PermissiveSocket : public Network::TransportSocket,
                         protected Logger::Loggable<Logger::Id::connection> {
public:
  PermissiveSocket(Network::TransportSocketPtr&& primary_transport_socket,
                   Network::TransportSocketPtr&& secondary_transport_socket);

  // Network::TransportSocket
  void setTransportSocketCallbacks(Network::TransportSocketCallbacks& callbacks) override;
  std::string protocol() const override;
  absl::string_view failureReason() const override;
  bool canFlushClose() override;
  void closeSocket(Network::ConnectionEvent) override;
  void onConnected() override;
  Network::IoResult doRead(Buffer::Instance& buffer) override;
  Network::IoResult doWrite(Buffer::Instance& buffer, bool end_stream) override;
  const Ssl::ConnectionInfo* ssl() const override;

  bool isFallback() const { return is_fallback_; }

private:
  void checkIoResult(Network::IoResult& io_result);

  bool is_fallback_{};
  ProxyTransportSocketCallbacksPtr callbacks_;
  Network::TransportSocketPtr primary_transport_socket_;
  Network::TransportSocketPtr secondary_transport_socket_;
};

class PermissiveSocketFactory : public Network::TransportSocketFactory {
public:
  PermissiveSocketFactory(Network::TransportSocketFactoryPtr&& primary_transport_socket_factory,
                          Network::TransportSocketFactoryPtr&& secondary_transport_socket_factory)
      : primary_transport_socket_factory_(std::move(primary_transport_socket_factory)),
        secondary_transport_socket_factory_(std::move(secondary_transport_socket_factory)) {}

  // Network::TransportSocketFactory
  Network::TransportSocketPtr
  createTransportSocket(Network::TransportSocketOptionsSharedPtr options) const override;
  bool implementsSecureTransport() const override;

private:
  Network::TransportSocketFactoryPtr primary_transport_socket_factory_;
  Network::TransportSocketFactoryPtr secondary_transport_socket_factory_;
};

} // namespace Permissive
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy