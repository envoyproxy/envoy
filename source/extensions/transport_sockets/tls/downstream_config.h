#pragma once

#include "envoy/registry/registry.h"
#include "envoy/server/transport_socket_config.h"

#include "source/extensions/transport_sockets/tls/config.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Tls {

class DownstreamSslSocketFactory
    : public Server::Configuration::DownstreamTransportSocketConfigFactory,
      public SslSocketConfigFactory {
public:
  absl::StatusOr<Network::DownstreamTransportSocketFactoryPtr>
  createTransportSocketFactory(const Protobuf::Message& config,
                               Server::Configuration::TransportSocketFactoryContext& context,
                               const std::vector<std::string>& server_names) override;
  ProtobufTypes::MessagePtr createEmptyConfigProto() override;
};

DECLARE_FACTORY(DownstreamSslSocketFactory);

} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
