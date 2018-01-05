#include "server/config/network/raw_buffer_socket.h"

#include "envoy/registry/registry.h"

#include "common/network/raw_buffer_socket.h"

namespace Envoy {
namespace Server {
namespace Configuration {

Network::TransportSocketFactoryPtr
RawBufferSocketFactory::createTransportSocketFactory(const Protobuf::Message&,
                                                     TransportSocketFactoryContext&) {
  return std::make_unique<Network::RawBufferSocketFactory>();
}

ProtobufTypes::MessagePtr RawBufferSocketFactory::createEmptyConfigProto() {
  return std::make_unique<ProtobufWkt::Empty>();
}

static Registry::RegisterFactory<UpstreamRawBufferSocketFactory,
                                 UpstreamTransportSocketConfigFactory>
    upstream_registered_;

} // namespace Configuration
} // namespace Server
} // namespace Envoy
