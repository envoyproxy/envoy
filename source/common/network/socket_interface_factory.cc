#include "common/network/socket_interface_factory.h"

#include "envoy/config/core/v3/socket_interface.pb.h"
#include "envoy/registry/registry.h"

#include "common/network/socket_interface_impl.h"

namespace Envoy {
namespace Network {

class SocketInterfaceExtension : public Server::BootstrapExtension {};

Server::BootstrapExtensionPtr
SocketInterfaceFactory::createBootstrapExtension(const Protobuf::Message& config,
                                                 Server::Configuration::ServerFactoryContext&) {
  const auto* proto = dynamic_cast<const envoy::config::core::v3::SocketInterfaceConfig*>(&config);
  switch (proto->type()) {
  case envoy::config::core::v3::SocketInterfaceConfig::DEFAULT:
    // no need to clear and re-initialize the SocketInterfaceSingleton
    break;
  default:
    break;
  }
  return std::make_unique<SocketInterfaceExtension>();
}

ProtobufTypes::MessagePtr SocketInterfaceFactory::createEmptyConfigProto() {
  return std::make_unique<envoy::config::core::v3::SocketInterfaceConfig>();
}

REGISTER_FACTORY(SocketInterfaceFactory, Server::Configuration::BootstrapExtensionFactory);

} // namespace Network
} // namespace Envoy