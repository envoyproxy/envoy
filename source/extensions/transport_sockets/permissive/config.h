#pragma once

#include "envoy/registry/registry.h"
#include "envoy/server/transport_socket_config.h"

#include "extensions/transport_sockets/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Permissive {

class PermissiveSocketConfigFactory
    : public virtual Server::Configuration::TransportSocketConfigFactory {
public:
  virtual ~PermissiveSocketConfigFactory() {}
  std::string name() const override { return TransportSocketNames::get().Permissive; }
};

// Permissive socket is only used for upstream connection.
class UpstreamPermissiveSocketConfigFactory
    : public Server::Configuration::UpstreamTransportSocketConfigFactory,
      public PermissiveSocketConfigFactory {
public:
  Network::TransportSocketFactoryPtr createTransportSocketFactory(
      const Protobuf::Message& config,
      Server::Configuration::TransportSocketFactoryContext& context) override;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override;
};

DECLARE_FACTORY(UpstreamPermissiveSocketConfigFactory);

} // namespace Permissive
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy