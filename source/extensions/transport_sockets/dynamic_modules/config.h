#pragma once

#include <memory>
#include <string>
#include <vector>

#include "envoy/network/transport_socket.h"
#include "envoy/registry/registry.h"
#include "envoy/server/transport_socket_config.h"

#include "source/common/common/statusor.h"
#include "source/extensions/dynamic_modules/abi.h"
#include "source/extensions/dynamic_modules/dynamic_modules.h"
#include "source/extensions/dynamic_modules/transport_socket_abi.h"
#include "source/extensions/transport_sockets/common/passthrough.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace DynamicModules {

// Function pointer types for transport socket ABI functions.
using OnTransportSocketFactoryConfigNewType =
    decltype(&envoy_dynamic_module_on_transport_socket_factory_config_new);
using OnTransportSocketFactoryConfigDestroyType =
    decltype(&envoy_dynamic_module_on_transport_socket_factory_config_destroy);
using OnTransportSocketNewType = decltype(&envoy_dynamic_module_on_transport_socket_new);
using OnTransportSocketDestroyType = decltype(&envoy_dynamic_module_on_transport_socket_destroy);
using OnTransportSocketSetCallbacksType =
    decltype(&envoy_dynamic_module_on_transport_socket_set_callbacks);
using OnTransportSocketOnConnectedType =
    decltype(&envoy_dynamic_module_on_transport_socket_on_connected);
using OnTransportSocketDoReadType = decltype(&envoy_dynamic_module_on_transport_socket_do_read);
using OnTransportSocketDoWriteType = decltype(&envoy_dynamic_module_on_transport_socket_do_write);
using OnTransportSocketCloseType = decltype(&envoy_dynamic_module_on_transport_socket_close);
using OnTransportSocketGetProtocolType =
    decltype(&envoy_dynamic_module_on_transport_socket_get_protocol);
using OnTransportSocketGetFailureReasonType =
    decltype(&envoy_dynamic_module_on_transport_socket_get_failure_reason);
using OnTransportSocketCanFlushCloseType =
    decltype(&envoy_dynamic_module_on_transport_socket_can_flush_close);

/**
 * Configuration for transport socket factory based on a dynamic module.
 * This will be owned by the factory and used to create transport sockets.
 *
 * Note: Symbol resolution and in-module config creation are done in the
 * newDynamicModuleTransportSocketFactoryConfig() to provide graceful error handling.
 */
class DynamicModuleTransportSocketFactoryConfig {
public:
  DynamicModuleTransportSocketFactoryConfig(
      const std::string& socket_name, const std::string& socket_config, bool is_upstream,
      Envoy::Extensions::DynamicModules::DynamicModulePtr dynamic_module);

  ~DynamicModuleTransportSocketFactoryConfig();

  // The corresponding in-module configuration.
  envoy_dynamic_module_type_transport_socket_factory_config_module_ptr in_module_config_ = nullptr;

  // Function pointers for the module related to the transport socket.
  OnTransportSocketFactoryConfigDestroyType on_factory_config_destroy_ = nullptr;
  OnTransportSocketNewType on_transport_socket_new_ = nullptr;
  OnTransportSocketDestroyType on_transport_socket_destroy_ = nullptr;
  OnTransportSocketSetCallbacksType on_transport_socket_set_callbacks_ = nullptr;
  OnTransportSocketOnConnectedType on_transport_socket_on_connected_ = nullptr;
  OnTransportSocketDoReadType on_transport_socket_do_read_ = nullptr;
  OnTransportSocketDoWriteType on_transport_socket_do_write_ = nullptr;
  OnTransportSocketCloseType on_transport_socket_close_ = nullptr;
  OnTransportSocketGetProtocolType on_transport_socket_get_protocol_ = nullptr;
  OnTransportSocketGetFailureReasonType on_transport_socket_get_failure_reason_ = nullptr;
  OnTransportSocketCanFlushCloseType on_transport_socket_can_flush_close_ = nullptr;

  bool is_upstream() const { return is_upstream_; }
  const std::string& socket_name() const { return socket_name_; }

private:
  friend absl::StatusOr<std::shared_ptr<DynamicModuleTransportSocketFactoryConfig>>
  newDynamicModuleTransportSocketFactoryConfig(
      const std::string& socket_name, const std::string& socket_config, bool is_upstream,
      Envoy::Extensions::DynamicModules::DynamicModulePtr dynamic_module);

  const std::string socket_name_;
  const std::string socket_config_;
  const bool is_upstream_;
  Envoy::Extensions::DynamicModules::DynamicModulePtr dynamic_module_;
};

using DynamicModuleTransportSocketFactoryConfigSharedPtr =
    std::shared_ptr<DynamicModuleTransportSocketFactoryConfig>;

/**
 * Creates a new DynamicModuleTransportSocketFactoryConfig for given configuration.
 */
absl::StatusOr<DynamicModuleTransportSocketFactoryConfigSharedPtr>
newDynamicModuleTransportSocketFactoryConfig(
    const std::string& socket_name, const std::string& socket_config, bool is_upstream,
    Envoy::Extensions::DynamicModules::DynamicModulePtr dynamic_module);

/**
 * Base class for transport socket config factories.
 */
class DynamicModuleTransportSocketConfigFactory
    : public virtual Server::Configuration::TransportSocketConfigFactory {
public:
  std::string name() const override { return "envoy.transport_sockets.dynamic_modules"; }
  ProtobufTypes::MessagePtr createEmptyConfigProto() override;
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
