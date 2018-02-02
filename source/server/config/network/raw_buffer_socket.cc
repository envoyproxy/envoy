#include "server/config/network/raw_buffer_socket.h"

#include "envoy/registry/registry.h"

#include "common/network/raw_buffer_socket.h"

namespace Envoy {
namespace Server {
namespace Configuration {

Network::TransportSocketFactoryPtr
UpstreamRawBufferSocketFactory::createTransportSocketFactory(const Protobuf::Message&,
                                                             TransportSocketFactoryContext&) {
  return std::make_unique<Network::RawBufferSocketFactory>();
}

Network::TransportSocketFactoryPtr DownstreamRawBufferSocketFactory::createTransportSocketFactory(
    const std::string&, const std::vector<std::string>&, bool, const Protobuf::Message&,
    TransportSocketFactoryContext&) {
  return std::make_unique<Network::RawBufferSocketFactory>();
}

ProtobufTypes::MessagePtr RawBufferSocketFactory::createEmptyConfigProto() {
  return std::make_unique<ProtobufWkt::Empty>();
}

static Registry::RegisterFactory<UpstreamRawBufferSocketFactory,
                                 UpstreamTransportSocketConfigFactory>
    upstream_registered_;

static Registry::RegisterFactory<DownstreamRawBufferSocketFactory,
                                 DownstreamTransportSocketConfigFactory>
    downstream_registered_;

} // namespace Configuration
} // namespace Server
} // namespace Envoy
