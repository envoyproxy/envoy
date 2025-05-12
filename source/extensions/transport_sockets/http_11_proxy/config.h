#pragma once

#include "envoy/server/transport_socket_config.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Http11Connect {

/**
 * Config registration for the upstream HTTP/1.1 proxy transport socket factory.
 * @see TransportSocketConfigFactory.
 */
class UpstreamHttp11ConnectSocketConfigFactory
    : public Server::Configuration::UpstreamTransportSocketConfigFactory {
public:
  std::string name() const override { return "envoy.transport_sockets.http_11_proxy"; }
  ProtobufTypes::MessagePtr createEmptyConfigProto() override;
  absl::StatusOr<Network::UpstreamTransportSocketFactoryPtr> createTransportSocketFactory(
      const Protobuf::Message& config,
      Server::Configuration::TransportSocketFactoryContext& context) override;
};

DECLARE_FACTORY(UpstreamHttp11ConnectSocketConfigFactory);

} // namespace Http11Connect
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
