#pragma once

#include "envoy/server/transport_socket_config.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace ProxyProtocol {

/**
 * Config registration for the proxy protocol wrapper for transport socket factory.
 * @see TransportSocketConfigFactory.
 */
class UpstreamProxyProtocolSocketConfigFactory
    : public Server::Configuration::UpstreamTransportSocketConfigFactory {
public:
  std::string name() const override { return "envoy.transport_sockets.upstream_proxy_protocol"; }
  ProtobufTypes::MessagePtr createEmptyConfigProto() override;
  Network::UpstreamTransportSocketFactoryPtr createTransportSocketFactory(
      const Protobuf::Message& config,
      Server::Configuration::TransportSocketFactoryContext& context) override;
};

} // namespace ProxyProtocol
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
