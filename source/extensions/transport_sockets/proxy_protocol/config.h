#pragma once

#include "envoy/server/transport_socket_config.h"

#include "extensions/transport_sockets/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace ProxyProtocol {

/**
 * Config registration for the tap wrapper for transport socket factory.
 * @see TransportSocketConfigFactory.
 */
class ProxyProtocolSocketConfigFactory
    : public Server::Configuration::UpstreamTransportSocketConfigFactory {
public:
  ~ProxyProtocolSocketConfigFactory() override = default;
  std::string name() const override { return TransportSocketNames::get().ProxyProtocol; }
  ProtobufTypes::MessagePtr createEmptyConfigProto() override;
  Network::TransportSocketFactoryPtr createTransportSocketFactory(
      const Protobuf::Message& config,
      Server::Configuration::TransportSocketFactoryContext& context) override;
};

} // namespace ProxyProtocol
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
