#pragma once

#include <memory>
#include <string>

#include "envoy/buffer/buffer.h"
#include "envoy/network/transport_socket.h"
#include "envoy/upstream/host_description.h"

#include "source/common/common/logger.h"
#include "source/common/common/statusor.h"
#include "source/common/network/transport_socket_options_impl.h"
#include "source/extensions/dynamic_modules/abi/abi.h"
#include "source/extensions/dynamic_modules/dynamic_modules.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace DynamicModules {

// Function pointer types for the transport socket ABI hooks.
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
using OnTransportSocketStartSecureTransportType =
    decltype(&envoy_dynamic_module_on_transport_socket_start_secure_transport);

/**
 * Configuration holding the resolved dynamic module, ABI function pointers, and the in-module
 * factory configuration. This is shared between the factory and every transport socket it creates,
 * which keeps the module loaded for as long as any socket is alive.
 */
class DynamicModuleTransportSocketConfig {
public:
  DynamicModuleTransportSocketConfig(
      bool implements_secure_transport,
      Envoy::Extensions::DynamicModules::DynamicModulePtr dynamic_module);

  ~DynamicModuleTransportSocketConfig();

  // The corresponding in-module factory configuration.
  envoy_dynamic_module_type_transport_socket_factory_config_module_ptr in_module_config_ = nullptr;

  // Resolved function pointers. All are guaranteed non-null after successful creation.
  OnTransportSocketFactoryConfigDestroyType on_factory_config_destroy_ = nullptr;
  OnTransportSocketNewType on_new_ = nullptr;
  OnTransportSocketDestroyType on_destroy_ = nullptr;
  OnTransportSocketSetCallbacksType on_set_callbacks_ = nullptr;
  OnTransportSocketOnConnectedType on_connected_ = nullptr;
  OnTransportSocketDoReadType on_do_read_ = nullptr;
  OnTransportSocketDoWriteType on_do_write_ = nullptr;
  OnTransportSocketCloseType on_close_ = nullptr;
  OnTransportSocketGetProtocolType on_get_protocol_ = nullptr;
  OnTransportSocketGetFailureReasonType on_get_failure_reason_ = nullptr;
  OnTransportSocketCanFlushCloseType on_can_flush_close_ = nullptr;
  OnTransportSocketStartSecureTransportType on_start_secure_transport_ = nullptr;

  bool implementsSecureTransport() const { return implements_secure_transport_; }

private:
  const bool implements_secure_transport_;
  Envoy::Extensions::DynamicModules::DynamicModulePtr dynamic_module_;
};

using DynamicModuleTransportSocketConfigSharedPtr =
    std::shared_ptr<DynamicModuleTransportSocketConfig>;

/**
 * Creates a new DynamicModuleTransportSocketConfig. Resolves all ABI symbols and creates the
 * in-module factory configuration. Returns an error if symbol resolution or module initialization
 * fails.
 */
absl::StatusOr<DynamicModuleTransportSocketConfigSharedPtr> newDynamicModuleTransportSocketConfig(
    const std::string& socket_name, const std::string& socket_config, bool is_upstream,
    bool implements_secure_transport,
    Envoy::Extensions::DynamicModules::DynamicModulePtr dynamic_module);

/**
 * A transport socket backed by a dynamic module. Corresponds to a single connection and runs
 * entirely on that connection's worker thread.
 */
class DynamicModuleTransportSocket : public Network::TransportSocket,
                                     public Logger::Loggable<Logger::Id::dynamic_modules> {
public:
  DynamicModuleTransportSocket(DynamicModuleTransportSocketConfigSharedPtr config);
  ~DynamicModuleTransportSocket() override;

  // Network::TransportSocket
  void setTransportSocketCallbacks(Network::TransportSocketCallbacks& callbacks) override;
  std::string protocol() const override;
  absl::string_view failureReason() const override;
  bool canFlushClose() override;
  void closeSocket(Network::ConnectionEvent event, bool abort_reset) override;
  Network::IoResult doRead(Buffer::Instance& buffer) override;
  Network::IoResult doWrite(Buffer::Instance& buffer, bool end_stream) override;
  void onConnected() override;
  Ssl::ConnectionInfoConstSharedPtr ssl() const override { return nullptr; }
  bool startSecureTransport() override;
  void configureInitialCongestionWindow(uint64_t, std::chrono::microseconds) override {}

  // Accessors for the ABI callbacks. These are only valid on the connection's worker thread.
  Network::TransportSocketCallbacks* callbacks() const { return callbacks_; }
  Buffer::Instance* currentReadBuffer() const { return current_read_buffer_; }
  Buffer::Instance* currentWriteBuffer() const { return current_write_buffer_; }

private:
  const DynamicModuleTransportSocketConfigSharedPtr config_;
  envoy_dynamic_module_type_transport_socket_module_ptr in_module_socket_ = nullptr;
  Network::TransportSocketCallbacks* callbacks_ = nullptr;
  // The buffer passed to the active do_read or do_write call. Set for the duration of that call so
  // that the buffer callbacks can reach the right buffer, and reset afterwards.
  Buffer::Instance* current_read_buffer_ = nullptr;
  Buffer::Instance* current_write_buffer_ = nullptr;
  // Backing store for the string view returned by failureReason.
  mutable std::string failure_reason_;
};

/**
 * Network factory that creates downstream dynamic module transport sockets.
 */
class DynamicModuleDownstreamTransportSocketFactory
    : public Network::DownstreamTransportSocketFactory {
public:
  DynamicModuleDownstreamTransportSocketFactory(DynamicModuleTransportSocketConfigSharedPtr config)
      : config_(std::move(config)) {}

  Network::TransportSocketPtr createDownstreamTransportSocket() const override;
  bool implementsSecureTransport() const override { return config_->implementsSecureTransport(); }

private:
  const DynamicModuleTransportSocketConfigSharedPtr config_;
};

/**
 * Network factory that creates upstream dynamic module transport sockets.
 */
class DynamicModuleUpstreamTransportSocketFactory
    : public Network::CommonUpstreamTransportSocketFactory {
public:
  DynamicModuleUpstreamTransportSocketFactory(DynamicModuleTransportSocketConfigSharedPtr config)
      : config_(std::move(config)) {}

  Network::TransportSocketPtr
  createTransportSocket(Network::TransportSocketOptionsConstSharedPtr options,
                        Upstream::HostDescriptionConstSharedPtr host) const override;
  bool implementsSecureTransport() const override { return config_->implementsSecureTransport(); }
  absl::string_view defaultServerNameIndication() const override { return ""; }

private:
  const DynamicModuleTransportSocketConfigSharedPtr config_;
};

} // namespace DynamicModules
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
