#pragma once

#include <optional>
#include <string>

#include "envoy/extensions/network/socket_interface/dynamic_modules/v3/dynamic_modules.pb.h"
#include "envoy/server/bootstrap_extension_config.h"

#include "source/common/common/logger.h"
#include "source/common/network/socket_interface_impl.h"
#include "source/extensions/dynamic_modules/abi/abi.h"
#include "source/extensions/network/socket_interface/dynamic_modules/dynamic_module_io_handle.h"

#include "absl/status/statusor.h"

namespace Envoy {
namespace Extensions {
namespace Network {
namespace SocketInterface {
namespace DynamicModules {

// The interface types live in ::Envoy::Network, which the enclosing namespace name shadows.
namespace Network = ::Envoy::Network;

class DynamicModuleSocketInterface;

/**
 * Bootstrap extension that owns the module configuration of a dynamic module socket interface.
 * The interface and its extension are created once at server startup, before any worker runs,
 * and their lifetime spans the whole server.
 */
class DynamicModuleSocketInterfaceExtension : public Network::SocketInterfaceExtension {
public:
  DynamicModuleSocketInterfaceExtension(DynamicModuleSocketInterface& sock_interface,
                                        DynamicModuleSocketInterfaceConfigSharedPtr config);

  ~DynamicModuleSocketInterfaceExtension() override;

  // Network::SocketInterfaceExtension
  void onServerInitialized(Server::Instance&) override;
  void onWorkerThreadInitialized() override;

  const DynamicModuleSocketInterfaceConfigSharedPtr& config() const { return config_; }

private:
  const DynamicModuleSocketInterfaceConfigSharedPtr config_;
};

/**
 * Socket interface implemented by a dynamic module. The module creates the socket for the
 * addresses that select this interface and, when it implements the I/O hooks, performs its own
 * I/O on it through the delegating handle.
 */
class DynamicModuleSocketInterface : public Network::SocketInterfaceImpl,
                                     public Logger::Loggable<Logger::Id::dynamic_modules> {
public:
  // Network::SocketInterface
  Network::IoHandlePtr socket(Network::Socket::Type socket_type, Network::Address::Type addr_type,
                              Network::Address::IpVersion version, bool socket_v6only,
                              const Network::SocketCreationOptions& options) const override;
  Network::IoHandlePtr socket(Network::Socket::Type socket_type,
                              const Network::Address::InstanceConstSharedPtr addr,
                              const Network::SocketCreationOptions& options) const override;
  bool ipFamilySupported(int domain) override;

  // Server::Configuration::BootstrapExtensionFactory
  Server::BootstrapExtensionCreationResult
  createBootstrapExtension(const Protobuf::Message& config,
                           Server::Configuration::ServerFactoryContext& context) override;
  ProtobufTypes::MessagePtr createEmptyConfigProto() override;
  std::string name() const override {
    return "envoy.extensions.network.socket_interface.dynamic_modules";
  }

  // Non-owning back pointer to the bootstrap extension that owns the module configuration. Set
  // once when the extension is created at startup, before any worker runs, and cleared when it
  // is destroyed. While it is null, sockets fall through to the standard datapath.
  DynamicModuleSocketInterfaceExtension* extension_{nullptr};

private:
  // Creates a socket through the module and wraps it. Returns an Unimplemented error when the
  // module reported that it does not support the address family, so the caller can fall back to
  // the standard socket interface. Other failures return nullptr as the handle with an error
  // status, mirroring how the default interface reports a failed socket(2).
  absl::StatusOr<Network::IoHandlePtr>
  createModuleSocket(Network::Socket::Type socket_type, absl::string_view address, uint32_t port,
                     int domain, bool socket_v6only,
                     const Network::SocketCreationOptions& options) const;

  // Builds the address buffer arguments for the module from an address instance.
  void addressToModuleArgs(const Network::Address::InstanceConstSharedPtr& addr,
                           std::string& address, uint32_t& port, int& domain,
                           bool& socket_v6only) const;
};

DECLARE_FACTORY(DynamicModuleSocketInterface);

} // namespace DynamicModules
} // namespace SocketInterface
} // namespace Network
} // namespace Extensions
} // namespace Envoy
