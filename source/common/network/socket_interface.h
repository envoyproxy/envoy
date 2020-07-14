#pragma once

#include "envoy/config/typed_config.h"
#include "envoy/network/socket.h"
#include "envoy/registry/registry.h"
#include "envoy/server/bootstrap_extension_config.h"

#include "common/singleton/threadsafe_singleton.h"

#include "absl/container/flat_hash_map.h"

namespace Envoy {
namespace Network {

// Wrapper for SocketInterface instances returned by createBootstrapExtension() which must be
// implemented by all factories that derive SocketInterfaceBase
class SocketInterfaceExtension : public Server::BootstrapExtension {
public:
  SocketInterfaceExtension(SocketInterface& sock_interface) : sock_interface_(sock_interface) {}
  SocketInterface& socketInterface() { return sock_interface_; }

private:
  SocketInterface& sock_interface_;
};

// Class to be derived by all SocketInterface implementations.
//
// It acts both as a SocketInterface and as a BootstrapExtensionFactory. The latter is used, on the
// one hand, to configure and initialize the interface and, on the other, for SocketInterface lookup
// by leveraging the FactoryRegistry. As required for all bootstrap extensions, all derived classes
// should register via the REGISTER_FACTORY() macro as BootstrapExtensionFactory.
//
// SocketInterface instances can be retrieved using the factory name, i.e., string returned by
// name() function implemented by all classes that derive SocketInterfaceBase, via
// Network::socketInterface(). When instantiating addresses, address resolvers should
// set the socket interface field to the name of the socket interface implementation that should
// be used to create sockets for said addresses.
class SocketInterfaceBase : public SocketInterface,
                            public Server::Configuration::BootstrapExtensionFactory {};

/**
 * Lookup SocketInterface instance by name
 * @param name Name of the socket interface to be looked up
 * @return Pointer to @ref SocketInterface instance that registered using the name of nullptr
 */
static inline const SocketInterface* socketInterface(std::string name) {
  auto factory =
      Registry::FactoryRegistry<Server::Configuration::BootstrapExtensionFactory>::getFactory(name);
  return dynamic_cast<SocketInterface*>(factory);
}

using SocketInterfaceSingleton = InjectableSingleton<SocketInterface>;
using SocketInterfaceLoader = ScopedInjectableLoader<SocketInterface>;

} // namespace Network
} // namespace Envoy