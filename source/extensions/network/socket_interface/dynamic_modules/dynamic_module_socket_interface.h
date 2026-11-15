#pragma once

#include <string>

#include "envoy/extensions/network/socket_interface/dynamic_modules/v3/dynamic_modules.pb.h"
#include "envoy/server/bootstrap_extension_config.h"

#include "source/common/common/logger.h"
#include "source/common/network/socket_interface_impl.h"
#include "source/extensions/dynamic_modules/abi/abi.h"
#include "source/extensions/network/socket_interface/dynamic_modules/dynamic_module_io_handle.h"

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

  const DynamicModuleSocketInterfaceConfigSharedPtr& config() const { return config_; }

private:
  const DynamicModuleSocketInterfaceConfigSharedPtr config_;
};

/**
 * Socket interface implemented by a dynamic module. Only default_socket_interface selects it, so
 * the module is asked for every socket Envoy creates except the admin listener's. For each socket
 * the module creates the socket or leaves it to the standard socket interface.
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

  // Non-owning back pointer to the extension that owns the module configuration. Set before any
  // worker runs and cleared when the extension is destroyed; while null, sockets are standard.
  DynamicModuleSocketInterfaceExtension* extension_{nullptr};

private:
  // Returns nullptr when the module leaves the socket to the standard socket interface, and a
  // socket whose operations fail when the module failed to create it.
  Network::IoHandlePtr createModuleSocket(Network::Socket::Type socket_type,
                                          absl::string_view address, uint32_t port, int domain,
                                          bool socket_v6only,
                                          const Network::SocketCreationOptions& options,
                                          bool& use_default) const;

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
