#pragma once

#include "envoy/registry/registry.h"
#include "envoy/server/transport_socket_config.h"

#include "source/extensions/transport_sockets/tls/config.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Tls {

class UpstreamSslSocketFactory : public Server::Configuration::UpstreamTransportSocketConfigFactory,
                                 public SslSocketConfigFactory {
public:
  absl::StatusOr<Network::UpstreamTransportSocketFactoryPtr> createTransportSocketFactory(
      const Protobuf::Message& config,
      Server::Configuration::TransportSocketFactoryContext& context) override;
  ProtobufTypes::MessagePtr createEmptyConfigProto() override;
};

DECLARE_FACTORY(UpstreamSslSocketFactory);

} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
