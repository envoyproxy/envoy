#pragma once

#include "envoy/server/transport_socket_config.h"

#include "common/config/well_known_names.h"

namespace Envoy {
namespace Server {
namespace Configuration {

/**
 * Config registration for the SSL transport socket factory.
 * @see TransportSocketConfigFactory.
 */
class SslSocketConfigFactory : public virtual TransportSocketConfigFactory {
public:
  virtual ~SslSocketConfigFactory() {}
  std::string name() const override { return Config::TransportSocketNames::get().SSL; }
};

class UpstreamSslSocketFactory : public UpstreamTransportSocketConfigFactory,
                                 public SslSocketConfigFactory {
public:
  Network::TransportSocketFactoryPtr
  createTransportSocketFactory(const Protobuf::Message& config,
                               TransportSocketFactoryContext& context) override;
  ProtobufTypes::MessagePtr createEmptyConfigProto() override;
};

class DownstreamSslSocketFactory : public DownstreamTransportSocketConfigFactory,
                                   public SslSocketConfigFactory {
public:
  Network::TransportSocketFactoryPtr
  createTransportSocketFactory(const std::string& listener_name,
                               const std::vector<std::string>& server_names,
                               bool skip_context_update, const Protobuf::Message& config,
                               TransportSocketFactoryContext& context) override;
  ProtobufTypes::MessagePtr createEmptyConfigProto() override;
};

} // namespace Configuration
} // namespace Server
} // namespace Envoy
