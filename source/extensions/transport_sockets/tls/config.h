#pragma once

#include "envoy/registry/registry.h"
#include "envoy/server/transport_socket_config.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Tls {

/**
 * Config registration for the BoringSSL transport socket factory.
 * @see TransportSocketConfigFactory.
 */
class SslSocketConfigFactory : public virtual Server::Configuration::TransportSocketConfigFactory {
public:
  ~SslSocketConfigFactory() override = default;
  std::string name() const override { return "envoy.transport_sockets.tls"; }
};

} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
