#include "extensions/transport_sockets/starttls/starttls_socket.h"

#include <iostream>

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace StartTls {

// Switch clear-text to secure transport.
bool StartTlsSocket::startSecureTransport() {
  if (!using_ssl_) {
    ssl_socket_->setTransportSocketCallbacks(*callbacks_);
    ssl_socket_->onConnected();
    oper_socket_ = std::move(ssl_socket_);
    using_ssl_ = true;
  }
  return true;
}

Network::TransportSocketPtr ServerStartTlsSocketFactory::createTransportSocket(
    Network::TransportSocketOptionsSharedPtr transport_socket_options) const {
  return std::make_unique<StartTlsSocket>(
      config_, raw_socket_factory_->createTransportSocket(transport_socket_options),
      tls_socket_factory_->createTransportSocket(transport_socket_options),
      transport_socket_options);
}

} // namespace StartTls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
