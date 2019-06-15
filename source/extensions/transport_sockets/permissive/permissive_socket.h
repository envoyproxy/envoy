#pragma once

#include "envoy/buffer/buffer.h"
#include "envoy/network/connection.h"
#include "envoy/network/transport_socket.h"

#include "common/common/logger.h"

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
  PermissiveSocket(Network::TransportSocketPtr&& tls_transport_socket,
                   Network::TransportSocketPtr&& raw_buffer_transport_socket);

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

  bool isDowngraded() const { return downgraded_; }

private:
  void checkIoResult(Network::IoResult& io_result);

  bool downgraded_{};
  Network::TransportSocketCallbacks* callbacks_{};
  Network::TransportSocketPtr tls_transport_socket_;
  Network::TransportSocketPtr raw_buffer_transport_socket_;
};

class PermissiveSocketFactory : public Network::TransportSocketFactory {
public:
  PermissiveSocketFactory(Network::TransportSocketFactoryPtr&& tls_transport_socket_factory,
                          Network::TransportSocketFactoryPtr&& raw_buffer_transport_socket_factory)
      : tls_transport_socket_factory_(std::move(tls_transport_socket_factory)),
        raw_buffer_transport_socket_facotry_(std::move(raw_buffer_transport_socket_factory)) {}

  // Network::TransportSocketFactory
  Network::TransportSocketPtr
  createTransportSocket(Network::TransportSocketOptionsSharedPtr options) const override;
  bool implementsSecureTransport() const override;

private:
  Network::TransportSocketFactoryPtr tls_transport_socket_factory_;
  Network::TransportSocketFactoryPtr raw_buffer_transport_socket_facotry_;
};

} // namespace Permissive
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy