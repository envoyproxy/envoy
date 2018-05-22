#include "extensions/transport_sockets/raw_buffer/config.h"

#include "envoy/registry/registry.h"

#include "common/network/raw_buffer_socket.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace RawBuffer {

Network::TransportSocketFactoryPtr UpstreamRawBufferSocketFactory::createTransportSocketFactory(
    const Protobuf::Message&, Server::Configuration::TransportSocketFactoryContext&) {
  return std::make_unique<Network::RawBufferSocketFactory>();
}

Network::TransportSocketFactoryPtr DownstreamRawBufferSocketFactory::createTransportSocketFactory(
    const Protobuf::Message&, Server::Configuration::TransportSocketFactoryContext&,
    const std::vector<std::string>&) {
  return std::make_unique<Network::RawBufferSocketFactory>();
}

ProtobufTypes::MessagePtr RawBufferSocketFactory::createEmptyConfigProto() {
  return std::make_unique<ProtobufWkt::Empty>();
}

static Registry::RegisterFactory<UpstreamRawBufferSocketFactory,
                                 Server::Configuration::UpstreamTransportSocketConfigFactory>
    upstream_registered_;

static Registry::RegisterFactory<DownstreamRawBufferSocketFactory,
                                 Server::Configuration::DownstreamTransportSocketConfigFactory>
    downstream_registered_;

} // namespace RawBuffer
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
