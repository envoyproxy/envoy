#pragma once

#include "envoy/server/transport_socket_config.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Alts {

// ALTS config registry
class AltsTransportSocketConfigFactory
    : public virtual Server::Configuration::TransportSocketConfigFactory {
public:
  ProtobufTypes::MessagePtr createEmptyConfigProto() override;
  std::string name() const override { return "envoy.transport_sockets.alts"; }
};

class UpstreamAltsTransportSocketConfigFactory
    : public AltsTransportSocketConfigFactory,
      public Server::Configuration::UpstreamTransportSocketConfigFactory {
public:
  Network::UpstreamTransportSocketFactoryPtr
  createTransportSocketFactory(const Protobuf::Message&,
                               Server::Configuration::TransportSocketFactoryContext&) override;
};

class DownstreamAltsTransportSocketConfigFactory
    : public AltsTransportSocketConfigFactory,
      public Server::Configuration::DownstreamTransportSocketConfigFactory {
public:
  Network::DownstreamTransportSocketFactoryPtr
  createTransportSocketFactory(const Protobuf::Message&,
                               Server::Configuration::TransportSocketFactoryContext&,
                               const std::vector<std::string>&) override;
};

} // namespace Alts
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
