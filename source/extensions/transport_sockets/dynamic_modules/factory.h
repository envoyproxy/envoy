#pragma once

#include <string>
#include <vector>

#include "envoy/network/transport_socket.h"
#include "envoy/registry/registry.h"
#include "envoy/server/transport_socket_config.h"

#include "source/common/network/transport_socket_options_impl.h"
#include "source/extensions/transport_sockets/dynamic_modules/config.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace DynamicModules {

/**
 * Base class for transport socket config factories.
 * @see TransportSocketConfigFactory.
 */
class DynamicModuleTransportSocketConfigFactory
    : public virtual Server::Configuration::TransportSocketConfigFactory {
public:
  std::string name() const override { return "envoy.transport_sockets.dynamic_modules"; }
};

/**
 * Factory for creating upstream transport sockets based on dynamic modules.
 */
class UpstreamDynamicModuleTransportSocketFactory
    : public Network::CommonUpstreamTransportSocketFactory {
public:
  UpstreamDynamicModuleTransportSocketFactory(
      DynamicModuleTransportSocketFactoryConfigSharedPtr config, const std::string& sni,
      const std::vector<std::string>& alpn_protocols);

  bool implementsSecureTransport() const override { return true; }
  bool supportsAlpn() const override { return !alpn_protocols_.empty(); }
  absl::string_view defaultServerNameIndication() const override { return sni_; }
  void hashKey(std::vector<uint8_t>& key,
               Network::TransportSocketOptionsConstSharedPtr options) const override;

  Network::TransportSocketPtr
  createTransportSocket(Network::TransportSocketOptionsConstSharedPtr options,
                        Upstream::HostDescriptionConstSharedPtr host) const override;

  const DynamicModuleTransportSocketFactoryConfigSharedPtr& factoryConfig() const {
    return factory_config_;
  }

private:
  DynamicModuleTransportSocketFactoryConfigSharedPtr factory_config_;
  const std::string sni_;
  const std::vector<std::string> alpn_protocols_;
};

/**
 * Factory for creating downstream transport sockets based on dynamic modules.
 */
class DownstreamDynamicModuleTransportSocketFactory
    : public Network::DownstreamTransportSocketFactory {
public:
  DownstreamDynamicModuleTransportSocketFactory(
      DynamicModuleTransportSocketFactoryConfigSharedPtr config, bool require_client_certificate,
      const std::vector<std::string>& alpn_protocols);

  bool implementsSecureTransport() const override { return true; }
  Network::TransportSocketPtr createDownstreamTransportSocket() const override;

  const DynamicModuleTransportSocketFactoryConfigSharedPtr& factoryConfig() const {
    return factory_config_;
  }
  bool requireClientCertificate() const { return require_client_certificate_; }
  const std::vector<std::string>& alpnProtocols() const { return alpn_protocols_; }

private:
  DynamicModuleTransportSocketFactoryConfigSharedPtr factory_config_;
  const bool require_client_certificate_;
  const std::vector<std::string> alpn_protocols_;
};

/**
 * Config registration for upstream dynamic module transport sockets.
 */
class UpstreamDynamicModuleTransportSocketConfigFactory
    : public Server::Configuration::UpstreamTransportSocketConfigFactory,
      public DynamicModuleTransportSocketConfigFactory {
public:
  ProtobufTypes::MessagePtr createEmptyConfigProto() override;
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
  ProtobufTypes::MessagePtr createEmptyConfigProto() override;
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
