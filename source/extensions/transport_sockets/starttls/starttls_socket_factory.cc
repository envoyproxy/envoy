#include "source/extensions/transport_sockets/starttls/starttls_socket_factory.h"

#include "source/extensions/transport_sockets/starttls/starttls_socket.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace StartTls {

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
