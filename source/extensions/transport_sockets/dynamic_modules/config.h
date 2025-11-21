#pragma once

#include <memory>

#include "envoy/extensions/transport_sockets/dynamic_modules/v3/dynamic_modules.pb.h"
#include "envoy/network/transport_socket.h"
#include "envoy/server/transport_socket_config.h"
#include "envoy/stats/scope.h"

#include "source/extensions/dynamic_modules/dynamic_modules.h"
#include "source/extensions/dynamic_modules/transport_socket_abi.h"
#include "source/extensions/transport_sockets/dynamic_modules/stats.h"

namespace Envoy {
namespace Extensions {
namespace DynamicModules {
namespace TransportSockets {

// Function pointer type aliases for transport socket ABI functions.
using OnTransportSocketConfigNewType =
    decltype(&envoy_dynamic_module_on_transport_socket_config_new);
using OnTransportSocketConfigDestroyType =
    decltype(&envoy_dynamic_module_on_transport_socket_config_destroy);
using OnTransportSocketNewType = decltype(&envoy_dynamic_module_on_transport_socket_new);
using OnTransportSocketDestroyType = decltype(&envoy_dynamic_module_on_transport_socket_destroy);
using OnTransportSocketSetCallbacksType =
    decltype(&envoy_dynamic_module_on_transport_socket_set_callbacks);
using OnTransportSocketProtocolType = decltype(&envoy_dynamic_module_on_transport_socket_protocol);
using OnTransportSocketFailureReasonType =
    decltype(&envoy_dynamic_module_on_transport_socket_failure_reason);
using OnTransportSocketCanFlushCloseType =
    decltype(&envoy_dynamic_module_on_transport_socket_can_flush_close);
using OnTransportSocketCloseType = decltype(&envoy_dynamic_module_on_transport_socket_close);
using OnTransportSocketOnConnectedType =
    decltype(&envoy_dynamic_module_on_transport_socket_on_connected);
using OnTransportSocketDoReadType = decltype(&envoy_dynamic_module_on_transport_socket_do_read);
using OnTransportSocketDoWriteType = decltype(&envoy_dynamic_module_on_transport_socket_do_write);
using OnTransportSocketGetSslInfoType =
    decltype(&envoy_dynamic_module_on_transport_socket_get_ssl_info);
using OnTransportSocketStartSecureTransportType =
    decltype(&envoy_dynamic_module_on_transport_socket_start_secure_transport);
using OnTransportSocketConfigureInitialCongestionWindowType =
    decltype(&envoy_dynamic_module_on_transport_socket_configure_initial_congestion_window);

/**
 * Configuration for a dynamic module transport socket.
 *
 * This class holds the dynamic module instance, configuration data, and resolved function pointers
 * from the module. It manages the lifecycle of the in-module configuration and ensures that all
 * required ABI functions are resolved at configuration time.
 *
 * The configuration is shared across multiple transport socket instances created from the same
 * factory configuration.
 *
 * Thread Safety: This class is thread-safe for read operations after construction. The function
 * pointers and configuration data are immutable after initialization.
 */
class DynamicModuleTransportSocketConfig {
public:
  DynamicModuleTransportSocketConfig(DynamicModulePtr dynamic_module,
                                     const std::string& socket_name,
                                     const std::string& socket_config, bool is_upstream,
                                     Stats::Scope& stats_scope);

  ~DynamicModuleTransportSocketConfig();

  /**
   * Get the in-module configuration pointer.
   */
  envoy_dynamic_module_type_transport_socket_config_module_ptr getInModuleConfig() const {
    return in_module_config_;
  }

  /**
   * Get the socket name.
   */
  const std::string& getSocketName() const { return socket_name_; }

  /**
   * Check if this is an upstream configuration.
   */
  bool isUpstream() const { return is_upstream_; }

  /**
   * Get the stats for this transport socket configuration.
   */
  DynamicModuleTransportSocketStats& stats() { return stats_; }

  // The corresponding in-module configuration.
  envoy_dynamic_module_type_transport_socket_config_module_ptr in_module_config_ = nullptr;

  // Function pointers resolved from the module.
  OnTransportSocketConfigDestroyType on_transport_socket_config_destroy_ = nullptr;
  OnTransportSocketNewType on_transport_socket_new_ = nullptr;
  OnTransportSocketDestroyType on_transport_socket_destroy_ = nullptr;
  OnTransportSocketSetCallbacksType on_transport_socket_set_callbacks_ = nullptr;
  OnTransportSocketProtocolType on_transport_socket_protocol_ = nullptr;
  OnTransportSocketFailureReasonType on_transport_socket_failure_reason_ = nullptr;
  OnTransportSocketCanFlushCloseType on_transport_socket_can_flush_close_ = nullptr;
  OnTransportSocketCloseType on_transport_socket_close_ = nullptr;
  OnTransportSocketOnConnectedType on_transport_socket_on_connected_ = nullptr;
  OnTransportSocketDoReadType on_transport_socket_do_read_ = nullptr;
  OnTransportSocketDoWriteType on_transport_socket_do_write_ = nullptr;
  OnTransportSocketGetSslInfoType on_transport_socket_get_ssl_info_ = nullptr;
  OnTransportSocketStartSecureTransportType on_transport_socket_start_secure_transport_ = nullptr;
  OnTransportSocketConfigureInitialCongestionWindowType
      on_transport_socket_configure_initial_congestion_window_ = nullptr;

private:
  DynamicModulePtr dynamic_module_;
  std::string socket_name_;
  std::string socket_config_;
  bool is_upstream_;
  DynamicModuleTransportSocketStats stats_;
};

using DynamicModuleTransportSocketConfigSharedPtr =
    std::shared_ptr<DynamicModuleTransportSocketConfig>;

/**
 * Creates a new DynamicModuleTransportSocketConfig for given configuration.
 * @param socket_name the name of the socket.
 * @param socket_config the configuration for the module.
 * @param is_upstream true for upstream sockets, false for downstream.
 * @param dynamic_module the dynamic module to use.
 * @param stats_scope the stats scope for recording metrics.
 * @return a shared pointer to the new config object or an error if the module could not be loaded.
 */
absl::StatusOr<DynamicModuleTransportSocketConfigSharedPtr>
newDynamicModuleTransportSocketConfig(const absl::string_view socket_name,
                                      const absl::string_view socket_config, bool is_upstream,
                                      DynamicModulePtr dynamic_module, Stats::Scope& stats_scope);

/**
 * Factory for creating dynamic module upstream transport sockets.
 *
 * This factory creates transport socket instances that delegate their implementation to a dynamic
 * module loaded from a shared library. It supports configuration of SNI, ALPN protocols, and
 * module-specific configuration data.
 *
 * The factory maintains a shared configuration object that is passed to each transport socket
 * instance, ensuring efficient resource usage and consistent behavior across connections.
 */
class UpstreamDynamicModuleTransportSocketFactory : public Network::UpstreamTransportSocketFactory {
public:
  static absl::StatusOr<std::unique_ptr<UpstreamDynamicModuleTransportSocketFactory>>
  create(const envoy::extensions::transport_sockets::dynamic_modules::v3::
             DynamicModuleUpstreamTransportSocket& config,
         Server::Configuration::TransportSocketFactoryContext& context);

  // Network::UpstreamTransportSocketFactory
  Network::TransportSocketPtr
  createTransportSocket(Network::TransportSocketOptionsConstSharedPtr options,
                        Upstream::HostDescriptionConstSharedPtr host) const override;
  bool implementsSecureTransport() const override;
  bool supportsAlpn() const override { return !alpn_protocols_.empty(); }
  absl::string_view defaultServerNameIndication() const override { return sni_; }
  void hashKey(std::vector<uint8_t>& key,
               Network::TransportSocketOptionsConstSharedPtr options) const override;
  Envoy::Ssl::ClientContextSharedPtr sslCtx() override { return nullptr; }
  OptRef<const Ssl::ClientContextConfig> clientContextConfig() const override { return {}; }
#ifdef ENVOY_ENABLE_QUIC
  std::shared_ptr<quic::QuicCryptoClientConfig> getCryptoConfig() override { return nullptr; }
#endif

private:
  UpstreamDynamicModuleTransportSocketFactory(DynamicModuleTransportSocketConfigSharedPtr config,
                                              std::string sni,
                                              std::vector<std::string> alpn_protocols);

  DynamicModuleTransportSocketConfigSharedPtr config_;
  std::string sni_;
  std::vector<std::string> alpn_protocols_;
};

/**
 * Factory for creating dynamic module downstream transport sockets.
 *
 * This factory creates transport socket instances for accepting incoming connections where the
 * transport implementation is provided by a dynamic module. It supports configuration of client
 * certificate requirements, ALPN protocols, and module-specific configuration data.
 *
 * The factory maintains a shared configuration object that is passed to each transport socket
 * instance, ensuring efficient resource usage and consistent behavior across connections.
 */
class DownstreamDynamicModuleTransportSocketFactory
    : public Network::DownstreamTransportSocketFactory {
public:
  static absl::StatusOr<std::unique_ptr<DownstreamDynamicModuleTransportSocketFactory>>
  create(const envoy::extensions::transport_sockets::dynamic_modules::v3::
             DynamicModuleDownstreamTransportSocket& config,
         Server::Configuration::TransportSocketFactoryContext& context);

  // Network::DownstreamTransportSocketFactory
  Network::TransportSocketPtr createDownstreamTransportSocket() const override;
  bool implementsSecureTransport() const override;

private:
  DownstreamDynamicModuleTransportSocketFactory(DynamicModuleTransportSocketConfigSharedPtr config,
                                                bool require_client_certificate,
                                                std::vector<std::string> alpn_protocols);

  DynamicModuleTransportSocketConfigSharedPtr config_;
  [[maybe_unused]] bool require_client_certificate_;
  [[maybe_unused]] std::vector<std::string> alpn_protocols_;
};

/**
 * Config factory for registering upstream dynamic module transport socket.
 */
class UpstreamDynamicModuleTransportSocketConfigFactory
    : public Server::Configuration::UpstreamTransportSocketConfigFactory {
public:
  std::string name() const override { return "envoy.transport_sockets.dynamic_modules"; }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override;

  absl::StatusOr<Network::UpstreamTransportSocketFactoryPtr> createTransportSocketFactory(
      const Protobuf::Message& config,
      Server::Configuration::TransportSocketFactoryContext& context) override;
};

/**
 * Config factory for registering downstream dynamic module transport socket.
 */
class DownstreamDynamicModuleTransportSocketConfigFactory
    : public Server::Configuration::DownstreamTransportSocketConfigFactory {
public:
  std::string name() const override { return "envoy.transport_sockets.dynamic_modules"; }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override;

  absl::StatusOr<Network::DownstreamTransportSocketFactoryPtr>
  createTransportSocketFactory(const Protobuf::Message& config,
                               Server::Configuration::TransportSocketFactoryContext& context,
                               const std::vector<std::string>& server_names) override;
};

} // namespace TransportSockets
} // namespace DynamicModules
} // namespace Extensions
} // namespace Envoy
