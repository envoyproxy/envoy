#pragma once

#include "envoy/registry/registry.h"
#include "envoy/server/transport_socket_config.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace StartTls {

class DownstreamStartTlsSocketFactory
    : public Server::Configuration::DownstreamTransportSocketConfigFactory {
public:
  Network::TransportSocketFactoryPtr
  createTransportSocketFactory(const Protobuf::Message& config,
                               Server::Configuration::TransportSocketFactoryContext& context,
                               const std::vector<std::string>& server_names) override;
  ProtobufTypes::MessagePtr createEmptyConfigProto() override;
  std::string name() const override { return "envoy.transport_sockets.starttls"; }
};

DECLARE_FACTORY(DownstreamStartTlsSocketFactory);

} // namespace StartTls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
