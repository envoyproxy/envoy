#include "contrib/vcl/source/config.h"

#include "contrib/envoy/extensions/vcl/v3alpha/vcl_socket_interface.pb.h"

namespace Envoy {
namespace Extensions {
namespace Network {
namespace Vcl {

VclSocketInterfaceExtension::VclSocketInterfaceExtension(
    Envoy::Network::SocketInterface& sock_interface)
    : Envoy::Network::SocketInterfaceExtension(sock_interface) {}

Server::BootstrapExtensionPtr
VclSocketInterface::createBootstrapExtension(const Protobuf::Message&,
                                             Server::Configuration::ServerFactoryContext& ctx) {

  vclSocketInterfaceInit(ctx.mainThreadDispatcher());
  return std::make_unique<VclSocketInterfaceExtension>(*this);
}

ProtobufTypes::MessagePtr VclSocketInterface::createEmptyConfigProto() {
  return std::make_unique<envoy::extensions::vcl::v3alpha::VclSocketInterface>();
}

REGISTER_FACTORY(VclSocketInterface, Server::Configuration::BootstrapExtensionFactory);

} // namespace Vcl
} // namespace Network
} // namespace Extensions
} // namespace Envoy
