#pragma once

#include "envoy/server/transport_socket_config.h"

#include "extensions/transport_sockets/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace SslTransport {

/**
 * Config registration for the BoringSSL transport socket factory.
 * @see TransportSocketConfigFactory.
 */
class SslSocketConfigFactory : public virtual Server::Configuration::TransportSocketConfigFactory {
public:
  virtual ~SslSocketConfigFactory() {}
  std::string name() const override { return TransportSocketNames::get().TLS; }
};

class UpstreamSslSocketFactory : public Server::Configuration::UpstreamTransportSocketConfigFactory,
                                 public SslSocketConfigFactory {
public:
  Network::TransportSocketFactoryPtr createTransportSocketFactory(
      const Protobuf::Message& config,
      Server::Configuration::TransportSocketFactoryContext& context) override;
  ProtobufTypes::MessagePtr createEmptyConfigProto() override;
};

class DownstreamSslSocketFactory
    : public Server::Configuration::DownstreamTransportSocketConfigFactory,
      public SslSocketConfigFactory {
public:
  Network::TransportSocketFactoryPtr
  createTransportSocketFactory(const Protobuf::Message& config,
                               Server::Configuration::TransportSocketFactoryContext& context,
                               const std::vector<std::string>& server_names) override;
  ProtobufTypes::MessagePtr createEmptyConfigProto() override;
};

} // namespace SslTransport
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
