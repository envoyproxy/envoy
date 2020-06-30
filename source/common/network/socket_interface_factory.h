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
// implemented by all factories that derive SocketInterfaceFactory
class SocketInterfaceExtension : public Server::BootstrapExtension {
public:
  SocketInterfaceExtension(SocketInterfacePtr sock_interface)
      : sock_interface_(std::move(sock_interface)) {}

private:
  SocketInterfacePtr sock_interface_;
};

// Class to be derived by all socket interface factories that create custom SocketInterface
// instances.
//
// The factories should be registered via the REGISTER_FACTORY() macro and are expected to be
// configured by means of bootstrap extensions. Factories must register their SocketInterface
// instances with the SocketInterfacesSingleton using Network::registerSocketInterface().
//
// SocketInterface instances can be retrieved using the factory name, i.e., string returned by
// name() function implemented by all factories that derive SocketInterfaceFactory, via
// Network::socketInterface(). When instantiating addresses, address resolvers should
// set the socket interface field to the name of the socket interface implementation that should
// be used to create sockets for said addresses.
class SocketInterfaceFactory : public Server::Configuration::BootstrapExtensionFactory {};

using SocketInterfacesMap = absl::flat_hash_map<std::string, SocketInterfacePtr>;
using SocketInterfacesSingleton = InjectableSingleton<SocketInterfacesMap>;
using SocketInterfacesLoader = ScopedInjectableLoader<SocketInterfacesMap>;

static inline const SocketInterface* socketInterface(std::string name) {
  auto it = SocketInterfacesSingleton::get().find(name);
  if (it == SocketInterfacesSingleton::get().end()) {
    return nullptr;
  }
  return it->second.get();
}

static inline void registerSocketInterface(std::string name, SocketInterface* sock_interface) {
  SocketInterfacesSingleton::get().emplace(std::make_pair(name, sock_interface));
}

using SocketInterfaceSingleton = InjectableSingleton<SocketInterface>;
using SocketInterfaceLoader = ScopedInjectableLoader<SocketInterface>;

} // namespace Network
} // namespace Envoy