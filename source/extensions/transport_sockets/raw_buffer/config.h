#pragma once

#include "envoy/server/transport_socket_config.h"

#include "extensions/transport_sockets/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace RawBuffer {

/**
 * Config registration for the raw buffer transport socket factory.
 * @see TransportSocketConfigFactory.
 */
class RawBufferSocketFactory : public virtual Server::Configuration::TransportSocketConfigFactory {
public:
  virtual ~RawBufferSocketFactory() {}
  std::string name() const override { return TransportSocketNames::get().RAW_BUFFER; }
  ProtobufTypes::MessagePtr createEmptyConfigProto() override;
};

class UpstreamRawBufferSocketFactory
    : public Server::Configuration::UpstreamTransportSocketConfigFactory,
      public RawBufferSocketFactory {
public:
  Network::TransportSocketFactoryPtr createTransportSocketFactory(
      const Protobuf::Message& config,
      Server::Configuration::TransportSocketFactoryContext& context) override;
};

class DownstreamRawBufferSocketFactory
    : public Server::Configuration::DownstreamTransportSocketConfigFactory,
      public RawBufferSocketFactory {
public:
  Network::TransportSocketFactoryPtr createTransportSocketFactory(
      const std::string& listener_name, const std::vector<std::string>& server_names,
      bool skip_context_update, const Protobuf::Message& config,
      Server::Configuration::TransportSocketFactoryContext& context) override;
};

} // namespace RawBuffer
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
