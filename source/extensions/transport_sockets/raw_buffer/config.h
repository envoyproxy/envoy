#pragma once

#include "envoy/registry/registry.h"
#include "envoy/server/transport_socket_config.h"

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
  std::string name() const override { return "envoy.transport_sockets.raw_buffer"; }
  ProtobufTypes::MessagePtr createEmptyConfigProto() override;
};

class UpstreamRawBufferSocketFactory
    : public Server::Configuration::UpstreamTransportSocketConfigFactory,
      public RawBufferSocketFactory {
public:
  absl::StatusOr<Network::UpstreamTransportSocketFactoryPtr> createTransportSocketFactory(
      const Protobuf::Message& config,
      Server::Configuration::TransportSocketFactoryContext& context) override;
};

class DownstreamRawBufferSocketFactory
    : public Server::Configuration::DownstreamTransportSocketConfigFactory,
      public RawBufferSocketFactory {
public:
  absl::StatusOr<Network::DownstreamTransportSocketFactoryPtr>
  createTransportSocketFactory(const Protobuf::Message& config,
                               Server::Configuration::TransportSocketFactoryContext& context,
                               const std::vector<std::string>& server_names) override;
};

DECLARE_FACTORY(UpstreamRawBufferSocketFactory);

DECLARE_FACTORY(DownstreamRawBufferSocketFactory);

} // namespace RawBuffer
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
