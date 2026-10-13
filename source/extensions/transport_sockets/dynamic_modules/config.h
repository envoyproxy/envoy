#pragma once

#include "envoy/registry/registry.h"
#include "envoy/server/transport_socket_config.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace DynamicModules {

/**
 * Config registration shared by the upstream and downstream dynamic module transport socket
 * factories.
 */
class DynamicModuleTransportSocketConfigFactory
    : public virtual Server::Configuration::TransportSocketConfigFactory {
public:
  std::string name() const override { return "envoy.transport_sockets.dynamic_modules"; }
  ProtobufTypes::MessagePtr createEmptyConfigProto() override;
};

/**
 * Config registration for upstream dynamic module transport sockets.
 */
class UpstreamDynamicModuleTransportSocketConfigFactory
    : public Server::Configuration::UpstreamTransportSocketConfigFactory,
      public DynamicModuleTransportSocketConfigFactory {
public:
  absl::StatusOr<Network::UpstreamTransportSocketFactoryPtr> createTransportSocketFactory(
      const Protobuf::Message& config,
      Server::Configuration::TransportSocketFactoryContext& context) override;
};

/**
 * Config registration for downstream dynamic module transport sockets.
 */
class DownstreamDynamicModuleTransportSocketConfigFactory
    : public Server::Configuration::DownstreamTransportSocketConfigFactory,
      public DynamicModuleTransportSocketConfigFactory {
public:
  absl::StatusOr<Network::DownstreamTransportSocketFactoryPtr>
  createTransportSocketFactory(const Protobuf::Message& config,
                               Server::Configuration::TransportSocketFactoryContext& context,
                               const std::vector<std::string>& server_names) override;
};

DECLARE_FACTORY(UpstreamDynamicModuleTransportSocketConfigFactory);
DECLARE_FACTORY(DownstreamDynamicModuleTransportSocketConfigFactory);

} // namespace DynamicModules
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
