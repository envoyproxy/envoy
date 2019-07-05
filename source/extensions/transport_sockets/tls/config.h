#pragma once

#include "envoy/registry/registry.h"
#include "envoy/server/transport_socket_config.h"

#include "extensions/transport_sockets/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Tls {

/**
 * Config registration for the BoringSSL transport socket factory.
 * @see TransportSocketConfigFactory.
 */
class SslSocketConfigFactory : public virtual Server::Configuration::TransportSocketConfigFactory {
public:
  ~SslSocketConfigFactory() override = default;
  std::string name() const override { return TransportSocketNames::get().Tls; }
};

class UpstreamSslSocketFactory : public Server::Configuration::UpstreamTransportSocketConfigFactory,
                                 public SslSocketConfigFactory {
public:
  Network::TransportSocketFactoryPtr createTransportSocketFactory(
      const Protobuf::Message& config,
      Server::Configuration::TransportSocketFactoryContext& context) override;
  ProtobufTypes::MessagePtr createEmptyConfigProto() override;
};

DECLARE_FACTORY(UpstreamSslSocketFactory);

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

DECLARE_FACTORY(DownstreamSslSocketFactory);

class SslContextManagerFactory : public Ssl::ContextManagerFactory {
public:
  Ssl::ContextManagerPtr createContextManager(TimeSource& time_source) override;
};

DECLARE_FACTORY(SslContextManagerFactory);

} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
