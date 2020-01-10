#include "extensions/transport_sockets/raw_buffer/config.h"

#include <iostream>

#include "envoy/config/transport_socket/raw_buffer/v2/raw_buffer.pb.h"
#include "envoy/config/transport_socket/raw_buffer/v2/raw_buffer.pb.validate.h"

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
  return std::make_unique<envoy::config::transport_socket::raw_buffer::v2::RawBuffer>();
}

REGISTER_FACTORY(UpstreamRawBufferSocketFactory,
                 Server::Configuration::UpstreamTransportSocketConfigFactory){"raw_buffer"};

REGISTER_FACTORY(DownstreamRawBufferSocketFactory,
                 Server::Configuration::DownstreamTransportSocketConfigFactory){"raw_buffer"};

} // namespace RawBuffer
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
