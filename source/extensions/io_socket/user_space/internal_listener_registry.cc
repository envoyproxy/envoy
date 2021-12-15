#include "source/extensions/io_socket/user_space/internal_listener_registry.h"

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace InternalListener {

void InternalListenerExtension::onServerInitialized() {}

Server::BootstrapExtensionPtr InternalListenerRegisteryFactory::createBootstrapExtension(
    const Protobuf::Message&, Server::Configuration::ServerFactoryContext&) {
  return nullptr;
}

} // namespace InternalListener
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
