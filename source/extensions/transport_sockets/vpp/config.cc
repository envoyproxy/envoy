#include "extensions/transport_sockets/vpp/config.h"

#include "envoy/registry/registry.h"

#include "common/vpp/vpp_socket.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Vpp {

Network::TransportSocketFactoryPtr UpstreamVppSocketFactory::createTransportSocketFactory(
    const Protobuf::Message&, Server::Configuration::TransportSocketFactoryContext&) {
  return std::make_unique<Envoy::Vpp::VppSocketFactory>();
}

Network::TransportSocketFactoryPtr DownstreamVppSocketFactory::createTransportSocketFactory(
    const std::string&, const std::vector<std::string>&, bool, const Protobuf::Message&,
    Server::Configuration::TransportSocketFactoryContext&) {
  return std::make_unique<Envoy::Vpp::VppSocketFactory>();
}

ProtobufTypes::MessagePtr VppSocketConfigFactory::createEmptyConfigProto() {
  return std::make_unique<ProtobufWkt::Empty>();
}

static Registry::RegisterFactory<UpstreamVppSocketFactory,
                                 Server::Configuration::UpstreamTransportSocketConfigFactory>
    upstream_registered_;

static Registry::RegisterFactory<DownstreamVppSocketFactory,
                                 Server::Configuration::DownstreamTransportSocketConfigFactory>
    downstream_registered_;

} // namespace Vpp
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
