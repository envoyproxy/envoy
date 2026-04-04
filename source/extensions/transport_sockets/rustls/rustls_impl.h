#pragma once

#include <memory>
#include <string>
#include <vector>

#include "envoy/network/transport_socket.h"
#include "envoy/registry/registry.h"
#include "envoy/server/transport_socket_config.h"
#include "envoy/upstream/host_description.h"

#include "source/common/common/logger.h"
#include "source/common/common/statusor.h"
#include "source/common/network/transport_socket_options_impl.h"
#include "source/extensions/dynamic_modules/abi/abi.h"
#include "source/extensions/dynamic_modules/dynamic_modules.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Rustls {

// Function pointer types for transport socket ABI event hooks.
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
 * Holds the loaded dynamic module, resolved ABI entry points, and in-module factory configuration
 * for the `rustls` transport socket. Shared across all sockets created from this config.
 */
class RustlsTransportSocketConfig {
public:
  /**
   * Loads the statically linked Rust module, resolves all ABI symbols, and creates the in-module
   * factory configuration. Returns an error if the module rejects the configuration.
   */
  static absl::StatusOr<std::shared_ptr<RustlsTransportSocketConfig>>
  create(bool is_upstream, absl::string_view socket_name, absl::string_view socket_config_bytes);

  /**
   * Constructor for test use, allowing creation without loading a real module. Callers must set
   * function pointers manually after construction.
   */
  RustlsTransportSocketConfig(bool is_upstream, std::string socket_name,
                              std::string socket_config_bytes,
                              DynamicModules::DynamicModulePtr dynamic_module);

  ~RustlsTransportSocketConfig();

  // Resolved ABI function pointers from the dynamic module.
  envoy_dynamic_module_type_transport_socket_factory_config_module_ptr in_module_factory_config_{
      nullptr};

  OnTransportSocketFactoryConfigDestroyType on_factory_config_destroy_{nullptr};
  OnTransportSocketNewType on_socket_new_{nullptr};
  OnTransportSocketDestroyType on_socket_destroy_{nullptr};
  OnTransportSocketSetCallbacksType on_set_callbacks_{nullptr};
  OnTransportSocketOnConnectedType on_on_connected_{nullptr};
  OnTransportSocketDoReadType on_do_read_{nullptr};
  OnTransportSocketDoWriteType on_do_write_{nullptr};
  OnTransportSocketCloseType on_close_{nullptr};
  OnTransportSocketGetProtocolType on_get_protocol_{nullptr};
  OnTransportSocketGetFailureReasonType on_get_failure_reason_{nullptr};
  OnTransportSocketCanFlushCloseType on_can_flush_close_{nullptr};

  bool isUpstream() const { return is_upstream_; }
  const std::string& socketName() const { return socket_name_; }
  const std::string& socketConfigBytes() const { return socket_config_bytes_; }

private:
  const bool is_upstream_;
  const std::string socket_name_;
  const std::string socket_config_bytes_;
  DynamicModules::DynamicModulePtr dynamic_module_;
};

using RustlsTransportSocketConfigSharedPtr = std::shared_ptr<RustlsTransportSocketConfig>;

/**
 * Transport socket implementation that delegates to the Rust module via the transport socket ABI.
 */
class RustlsTransportSocket : public Network::TransportSocket,
                              public Logger::Loggable<Logger::Id::dynamic_modules> {
public:
  RustlsTransportSocket(RustlsTransportSocketConfigSharedPtr config, bool is_upstream);
  ~RustlsTransportSocket() override;

  // Network::TransportSocket.
  void setTransportSocketCallbacks(Network::TransportSocketCallbacks& callbacks) override;
  std::string protocol() const override;
  absl::string_view failureReason() const override;
  bool canFlushClose() override;
  void closeSocket(Network::ConnectionEvent event) override;
  void onConnected() override;
  Network::IoResult doRead(Buffer::Instance& buffer) override;
  Network::IoResult doWrite(Buffer::Instance& buffer, bool end_stream) override;
  Ssl::ConnectionInfoConstSharedPtr ssl() const override { return nullptr; }
  bool startSecureTransport() override { return false; }
  void configureInitialCongestionWindow(uint64_t, std::chrono::microseconds) override {}

  envoy_dynamic_module_type_transport_socket_envoy_ptr thisAsEnvoyPtr() {
    return static_cast<envoy_dynamic_module_type_transport_socket_envoy_ptr>(this);
  }

  Network::TransportSocketCallbacks* transportCallbacks() { return callbacks_; }
  Buffer::Instance* activeReadBuffer() { return active_read_buffer_; }
  Buffer::Instance* activeWriteBuffer() { return active_write_buffer_; }

private:
  void refreshProtocolString() const;
  void refreshFailureReasonString() const;

  RustlsTransportSocketConfigSharedPtr config_;
  Network::TransportSocketCallbacks* callbacks_{nullptr};
  envoy_dynamic_module_type_transport_socket_module_ptr socket_module_{nullptr};
  Buffer::Instance* active_read_buffer_{nullptr};
  Buffer::Instance* active_write_buffer_{nullptr};
  mutable std::string protocol_storage_;
  mutable std::string failure_reason_storage_;
};

class RustlsUpstreamTransportSocketFactory : public Network::CommonUpstreamTransportSocketFactory {
public:
  RustlsUpstreamTransportSocketFactory(RustlsTransportSocketConfigSharedPtr config,
                                       std::string default_sni,
                                       std::vector<std::string> alpn_protocols,
                                       bool implements_secure_transport = false);

  Network::TransportSocketPtr
  createTransportSocket(Network::TransportSocketOptionsConstSharedPtr options,
                        Upstream::HostDescriptionConstSharedPtr host) const override;

  bool implementsSecureTransport() const override { return implements_secure_transport_; }
  absl::string_view defaultServerNameIndication() const override { return default_sni_; }
  bool supportsAlpn() const override { return !alpn_protocols_.empty(); }

  void hashKey(std::vector<uint8_t>& key,
               Network::TransportSocketOptionsConstSharedPtr options) const override;

private:
  RustlsTransportSocketConfigSharedPtr config_;
  const std::string default_sni_;
  const std::vector<std::string> alpn_protocols_;
  const bool implements_secure_transport_;
};

class RustlsDownstreamTransportSocketFactory : public Network::DownstreamTransportSocketFactory {
public:
  explicit RustlsDownstreamTransportSocketFactory(RustlsTransportSocketConfigSharedPtr config,
                                                  bool implements_secure_transport = false);

  Network::TransportSocketPtr createDownstreamTransportSocket() const override;
  bool implementsSecureTransport() const override { return implements_secure_transport_; }

private:
  RustlsTransportSocketConfigSharedPtr config_;
  const bool implements_secure_transport_;
};

/**
 * Config factory for upstream transport sockets using the `rustls` TLS library with optional `kTLS`
 * offload. Loads the Rust module statically by name and delegates I/O to the ABI.
 */
class RustlsUpstreamTransportSocketConfigFactory
    : public Server::Configuration::UpstreamTransportSocketConfigFactory {
public:
  std::string name() const override { return "envoy.transport_sockets.rustls"; }
  ProtobufTypes::MessagePtr createEmptyConfigProto() override;

  absl::StatusOr<Network::UpstreamTransportSocketFactoryPtr> createTransportSocketFactory(
      const Protobuf::Message& config,
      Server::Configuration::TransportSocketFactoryContext& context) override;
};

/**
 * Config factory for downstream transport sockets using the `rustls` TLS library with optional
 * `kTLS` offload.
 */
class RustlsDownstreamTransportSocketConfigFactory
    : public Server::Configuration::DownstreamTransportSocketConfigFactory {
public:
  std::string name() const override { return "envoy.transport_sockets.rustls"; }
  ProtobufTypes::MessagePtr createEmptyConfigProto() override;

  absl::StatusOr<Network::DownstreamTransportSocketFactoryPtr>
  createTransportSocketFactory(const Protobuf::Message& config,
                               Server::Configuration::TransportSocketFactoryContext& context,
                               const std::vector<std::string>& server_names) override;
};

DECLARE_FACTORY(RustlsUpstreamTransportSocketConfigFactory);
DECLARE_FACTORY(RustlsDownstreamTransportSocketConfigFactory);

} // namespace Rustls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
