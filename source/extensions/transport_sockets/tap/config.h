#pragma once

#include "envoy/server/transport_socket_config.h"

#include "extensions/transport_sockets/well_known_names.h"

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
  std::string name() const override { return TransportSocketNames::get().Tap; }
  ProtobufTypes::MessagePtr createEmptyConfigProto() override;
};

class UpstreamTapSocketConfigFactory
    : public Server::Configuration::UpstreamTransportSocketConfigFactory,
      public TapSocketConfigFactory {
public:
  Network::TransportSocketFactoryPtr createTransportSocketFactory(
      const Protobuf::Message& config,
      Server::Configuration::TransportSocketFactoryContext& context) override;
};

class DownstreamTapSocketConfigFactory
    : public Server::Configuration::DownstreamTransportSocketConfigFactory,
      public TapSocketConfigFactory {
public:
  Network::TransportSocketFactoryPtr
  createTransportSocketFactory(const Protobuf::Message& config,
                               Server::Configuration::TransportSocketFactoryContext& context,
                               const std::vector<std::string>& server_names) override;
};

} // namespace Tap
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
