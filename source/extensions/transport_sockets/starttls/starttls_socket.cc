#include "source/extensions/transport_sockets/starttls/starttls_socket.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace StartTls {

// Switch clear-text to secure transport.
bool StartTlsSocket::startSecureTransport() {
  if (!using_tls_) {
    tls_socket_->setTransportSocketCallbacks(callbacks_);
    tls_socket_->onConnected();
    // TODO(cpakulski): deleting active_socket_ assumes
    // that active_socket_ does not contain any buffered data.
    // Currently, active_socket_ is initialized to raw_buffer, which does not
    // buffer. If active_socket_ is initialized to a transport socket which
    // does buffering, it should be flushed before destroying or
    // flush should be called from destructor.
    active_socket_ = std::move(tls_socket_);
    using_tls_ = true;
  }
  return true;
}

Network::TransportSocketPtr StartTlsSocketFactory::createTransportSocket(
    Network::TransportSocketOptionsConstSharedPtr transport_socket_options,
    Upstream::HostDescriptionConstSharedPtr host) const {
  return std::make_unique<StartTlsSocket>(
      raw_socket_factory_->createTransportSocket(transport_socket_options, host),
      tls_socket_factory_->createTransportSocket(transport_socket_options, host),
      transport_socket_options);
}

Network::TransportSocketPtr
StartTlsDownstreamSocketFactory::createDownstreamTransportSocket() const {
  return std::make_unique<StartTlsSocket>(raw_socket_factory_->createDownstreamTransportSocket(),
                                          tls_socket_factory_->createDownstreamTransportSocket(),
                                          nullptr);
}

} // namespace StartTls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
