#pragma once

#include "envoy/server/transport_socket_config.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Tap {

/**
 * Config registration for the tap wrapper for transport socket factory.
 * @see TransportSocketConfigFactory.
 */
class TapSocketConfigFactory : public virtual Server::Configuration::TransportSocketConfigFactory {
public:
  std::string name() const override { return "envoy.transport_sockets.tap"; }
  ProtobufTypes::MessagePtr createEmptyConfigProto() override;
};

class UpstreamTapSocketConfigFactory
    : public Server::Configuration::UpstreamTransportSocketConfigFactory,
      public TapSocketConfigFactory {
public:
  Network::UpstreamTransportSocketFactoryPtr createTransportSocketFactory(
      const Protobuf::Message& config,
      Server::Configuration::TransportSocketFactoryContext& context) override;
};

class DownstreamTapSocketConfigFactory
    : public Server::Configuration::DownstreamTransportSocketConfigFactory,
      public TapSocketConfigFactory {
public:
  Network::DownstreamTransportSocketFactoryPtr
  createTransportSocketFactory(const Protobuf::Message& config,
                               Server::Configuration::TransportSocketFactoryContext& context,
                               const std::vector<std::string>& server_names) override;
};

} // namespace Tap
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
