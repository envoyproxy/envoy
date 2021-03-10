#pragma once

#include "envoy/registry/registry.h"
#include "envoy/server/transport_socket_config.h"

#include "extensions/transport_sockets/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace StartTls {

class BaseStartTlsSocketFactory
    : public virtual Server::Configuration::TransportSocketConfigFactory {
public:
  ProtobufTypes::MessagePtr createEmptyConfigProto() override;
  std::string name() const override { return TransportSocketNames::get().StartTls; }
};

class DownstreamStartTlsSocketFactory
    : public Server::Configuration::DownstreamTransportSocketConfigFactory,
      public BaseStartTlsSocketFactory {
public:
  Network::TransportSocketFactoryPtr
  createTransportSocketFactory(const Protobuf::Message& config,
                               Server::Configuration::TransportSocketFactoryContext& context,
                               const std::vector<std::string>& server_names) override;
};

class UpstreamStartTlsSocketFactory
    : public Server::Configuration::UpstreamTransportSocketConfigFactory,
      public BaseStartTlsSocketFactory {
public:
  Network::TransportSocketFactoryPtr
  createTransportSocketFactory(const Protobuf::Message& config,
                               Server::Configuration::TransportSocketFactoryContext& context) override;
};

DECLARE_FACTORY(DownstreamStartTlsSocketFactory);
DECLARE_FACTORY(UpstreamStartTlsSocketFactory);

} // namespace StartTls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
