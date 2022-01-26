#pragma once

#include "envoy/extensions/transport_sockets/internal/v3/internal_upstream.pb.h"
#include "envoy/server/transport_socket_config.h"

#include "source/extensions/transport_sockets/common/passthrough.h"
#include "source/extensions/transport_sockets/internal/internal.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Internal {

class InternalSocketFactory : public PassthroughFactory {
public:
  InternalSocketFactory(Server::Configuration::TransportSocketFactoryContext& context,
                        const envoy::extensions::transport_sockets::internal::v3::InternalUpstreamTransport& config,
                        Network::TransportSocketFactoryPtr&& inner_factory);

  Network::TransportSocketPtr
  createTransportSocket(Network::TransportSocketOptionsConstSharedPtr options) const override;

private:
  ConfigConstSharedPtr config_;
};

} // namespace Internal
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
