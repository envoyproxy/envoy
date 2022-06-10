#include "source/extensions/transport_sockets/raw_buffer/config.h"

#include <iostream>

#include "envoy/extensions/transport_sockets/raw_buffer/v3/raw_buffer.pb.h"
#include "envoy/extensions/transport_sockets/raw_buffer/v3/raw_buffer.pb.validate.h"

#include "source/common/network/raw_buffer_socket.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace RawBuffer {

Network::UpstreamTransportSocketFactoryPtr
UpstreamRawBufferSocketFactory::createTransportSocketFactory(
    const Protobuf::Message&, Server::Configuration::TransportSocketFactoryContext&) {
  return std::make_unique<Network::RawBufferSocketFactory>();
}

Network::DownstreamTransportSocketFactoryPtr
DownstreamRawBufferSocketFactory::createTransportSocketFactory(
    const Protobuf::Message&, Server::Configuration::TransportSocketFactoryContext&,
    const std::vector<std::string>&) {
  return std::make_unique<Network::RawBufferSocketFactory>();
}

ProtobufTypes::MessagePtr RawBufferSocketFactory::createEmptyConfigProto() {
  return std::make_unique<envoy::extensions::transport_sockets::raw_buffer::v3::RawBuffer>();
}

REGISTER_FACTORY(UpstreamRawBufferSocketFactory,
                 Server::Configuration::UpstreamTransportSocketConfigFactory){"raw_buffer"};

REGISTER_FACTORY(DownstreamRawBufferSocketFactory,
                 Server::Configuration::DownstreamTransportSocketConfigFactory){"raw_buffer"};

} // namespace RawBuffer
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
